// cl.input.c  -- builds an intended movement command to send to the server

//xxxxxx Move bob and pitch drifting code here and other stuff from view if needed

// Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All
// rights reserved.
#include "event_api.h"
#include "wrect.h"
#include "cl_dll.h"

#include <algorithm>

#include "hud.h"
#include "cl_util.h"
#include "camera.h"

#include "kbutton.h"
#include "pm_defs.h"
#include "pm_shared.h"
#include "cvardef.h"
#include "usercmd.h"
#include "const.h"
#include "camera.h"
#include "in_defs.h"
#include "view.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "Exports.h"
#include "cl_voice_status.h"
#include "hud/health.h"
#include "hud/menu.h"
#include "hud/spectator.h"
#include "com_model.h"

#include "vgui/client_viewport.h"

#ifndef _MOVEVARS_DECLARED_
struct movevars_s {
    float   gravity;
    float   stopspeed;
    float   maxspeed;
    float   spectatormaxspeed;
    float   accelerate;
    float   airaccelerate;
    float   wateraccelerate;
    float   friction;
    float   edgefriction;
    float   waterfriction;
    float   entgravity;
    float   bounce;
    float   stepsize;
    float   maxvelocity;
    float   zmax;
    float   waveHeight;
    int     footsteps;
    char    skyName[32];
    float   rollangle;
    float   rollspeed;
    float   skycolor_r;
    float   skycolor_g;
    float   skycolor_b;
    float   breakvavles;
    float   gargantua;
};
#define _MOVEVARS_DECLARED_
#endif

extern int g_iAlive;

extern int g_weaponselect;
extern cl_enginefunc_t gEngfuncs;
extern float g_lastFOV; // defined in hl_weapons.cpp; NOT wiped after use, unlike g_finalstate

bool g_bDecentJumped = false;
bool g_bLongJumped = false;
bool g_bBunnyhopJumped = false;
bool g_bJumpbugActive = false;
bool g_bSGSActive = false;

void JumpbugOn(void) {
    g_bJumpbugActive = true;
}

void JumpbugOff(void) {
    g_bJumpbugActive = false;
}

void SgsOn(void) {
    g_bSGSActive = true;
}

void SgsOff(void) {
    g_bSGSActive = false;
}

void IN_Init(void);
void IN_Move(float frametime, usercmd_t *cmd);
void IN_Shutdown(void);
void V_Init(void);
void VectorAngles(const float *forward, float *angles);
int CL_ButtonBits(int);

// xxx need client dll function to get and clear impuse
extern cvar_t *in_joystick;

int in_impulse = 0;
int in_cancel = 0;

cvar_t *m_pitch;
cvar_t *m_yaw;
cvar_t *m_forward;
cvar_t *m_side;

cvar_t *lookstrafe;
cvar_t *lookspring;
cvar_t *cl_pitchup;
cvar_t *cl_pitchdown;
cvar_t *cl_upspeed;
cvar_t *cl_forwardspeed;
cvar_t *cl_backspeed;
cvar_t *cl_sidespeed;
cvar_t *cl_movespeedkey;
cvar_t *cl_yawspeed;
cvar_t *cl_pitchspeed;
cvar_t *cl_anglespeedkey;
cvar_t *cl_vsmoothing;
cvar_t *cl_jumptype;

ConVar cl_autojump("cl_autojump", "0", FCVAR_BHL_ARCHIVE, "Jump automatically when ground is hit");
ConVar cl_autojump_priority("cl_autojump_priority", "0", FCVAR_BHL_ARCHIVE, "Autojump takes priority over ducktap");
ConVar cl_fastxbow("cl_fastxbow", "0", FCVAR_BHL_ARCHIVE, "Crossbow only: on a fresh left-click while unzoomed, automatically zoom then fire for a guaranteed lethal hit in HLDM (stays zoomed after; unzoom manually)");

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as a parameter to the command so it can be matched up with
the release.

state bit 0 is the current state of the key
state bit 1 is edge triggered on the up to down transition
state bit 2 is edge triggered on the down to up transition

===============================================================================
*/

kbutton_t in_mlook;
kbutton_t in_klook;
kbutton_t in_jlook;
kbutton_t in_left;
kbutton_t in_right;
kbutton_t in_forward;
kbutton_t in_back;
kbutton_t in_lookup;
kbutton_t in_lookdown;
kbutton_t in_moveleft;
kbutton_t in_moveright;
kbutton_t in_strafe;
kbutton_t in_speed;
kbutton_t in_use;
kbutton_t in_jump;
kbutton_t in_longjump;
kbutton_t in_attack;
kbutton_t in_attack2;
kbutton_t in_up;
kbutton_t in_down;
kbutton_t in_duck;
kbutton_t in_reload;
kbutton_t in_alt1;
kbutton_t in_score;
kbutton_t in_break;
kbutton_t in_graph; // Display the netgraph
kbutton_t in_ducktap;

int g_nSidePriority = 0;
int g_nForwardPriority = 0;

typedef struct kblist_s
{
	struct kblist_s *next;
	kbutton_t *pkey;
	char name[32];
} kblist_t;

kblist_t *g_kbkeys = NULL;

namespace autofuncs
{
static void HandleAutojump(usercmd_t *cmd)
{
	static bool s_bJumpWasDownLastFrame = false;

	bool inWater = PM_GetWaterLevel() > 1;
	bool isWalking = PM_GetMoveType() == MOVETYPE_WALK;
	bool shouldReleaseDuck = (!PM_GetOnGround() && !inWater && isWalking);

	if (cl_autojump.GetBool())
	{
		bool shouldReleaseJump = (!PM_GetOnGround() && !inWater && isWalking);

		/*
		 * Spam pressing and releasing jump if we're stuck in a spot where jumping still results in
		 * being onground in the end of the frame. Without this check, +jump would remain held and
		 * when the player exits this spot they would have to release and press the jump button to
		 * start jumping again. This also helps with exiting water or ladder right onto the ground.
		 */
		if (s_bJumpWasDownLastFrame && PM_GetOnGround() && !inWater && isWalking)
			shouldReleaseJump = true;

		if (shouldReleaseJump)
			cmd->buttons &= ~IN_JUMP;
	}

	s_bJumpWasDownLastFrame = ((cmd->buttons & IN_JUMP) != 0);
}

static void HandleDucktap(usercmd_t *cmd)
{
	static bool s_bDuckWasDownLastFrame = false;

	bool inWater = PM_GetWaterLevel() > 1;
	bool isWalking = PM_GetMoveType() == MOVETYPE_WALK;
	bool duckIsPressed = in_duck.state & 1;

	bool shouldReleaseDuck = (!PM_GetOnGround() && !inWater && isWalking && !duckIsPressed);

	if (s_bDuckWasDownLastFrame && PM_GetOnGround() && !inWater && isWalking)
		shouldReleaseDuck = true;

	if (shouldReleaseDuck)
	{
		cmd->buttons &= ~IN_DUCK;
		in_duck.state &= 0;
	}

	s_bDuckWasDownLastFrame = ((cmd->buttons & IN_DUCK) != 0);
}

// =================================================================================
// cl_fastxbow
// =================================================================================
// Reference: CCrossbow::PrimaryAttack/SecondaryAttack (crossbow.cpp).
//
//  - m_fInZoom (tied 1:1 to pev->fov) flips INSTANTLY inside SecondaryAttack(),
//    no delay on the flag itself.
//  - PrimaryAttack() just reads the CURRENT m_fInZoom: if true (and multiplayer),
//    it fires FireSniperBolt() (hitscan, real damage) instead of the slow,
//    exploding FireBolt().
//  - ItemPostFrame() only processes ONE of secondary/primary attack per command
//    (if/else), so zoom and shoot cannot be sent in the same frame - it has to
//    be zoom this frame, shoot next frame.
//
// No auto-unzoom: this only does zoom -> shoot. You stay zoomed afterwards and
// unzoom manually (+attack2) whenever you want, same as normal crossbow use.
//
// Equipped-weapon check uses the local VIEW MODEL (gEngfuncs.GetViewModel()),
// not CHudAmmo::m_pWeapon. CHudAmmo is only updated once the server's
// "CurWeapon" usermessage round-trips back, which lags a couple frames (more
// with real ping) behind a fast weapon switch - if you swap to the crossbow and
// click within that window, CHudAmmo can still report the PREVIOUS weapon, so
// the click goes through unmodified as a normal (weak) shot. The view model
// you're actually looking at is driven by prediction and changes the instant
// you switch - it's the same reason you always SEE your held weapon change
// immediately regardless of ping - so checking it directly closes that race.
enum class FastXbowState
{
	Idle,
	PendingShoot,
};

static bool IsCrossbowViewModelActive()
{
	cl_entity_t *pViewModel = gEngfuncs.GetViewModel();
	return pViewModel && pViewModel->model &&
		!_strnicmp(pViewModel->model->name, "models/v_crossbow.mdl", 22);
}

static void HandleFastXbow(usercmd_t *cmd)
{
	static FastXbowState s_state = FastXbowState::Idle;
	static bool s_bRealAttackWasDown = false;

	bool realAttackDown = (in_attack.state & 1) != 0;
	bool freshAttackPress = realAttackDown && !s_bRealAttackWasDown;
	s_bRealAttackWasDown = realAttackDown;

	bool isCrossbowEquipped = IsCrossbowViewModelActive();

	switch (s_state)
	{
	case FastXbowState::Idle:
	{
		bool isZoomed = (g_lastFOV != 0);

		if (freshAttackPress && cl_fastxbow.GetBool() && isCrossbowEquipped && !isZoomed)
		{
			// Hijack this click entirely: don't let an un-zoomed (weak,
			// exploding-bolt) shot go out on the same press.
			cmd->buttons &= ~IN_ATTACK;
			cmd->buttons |= IN_ATTACK2;
			s_state = FastXbowState::PendingShoot;
		}
		break;
	}

	case FastXbowState::PendingShoot:
		if (!isCrossbowEquipped)
		{
			// Weapon got switched away between the zoom press and now -
			// bail out instead of sending a stray shot at whatever's equipped.
			s_state = FastXbowState::Idle;
			break;
		}

		// The zoom press went out last frame and is now in effect. Fire the
		// actual shot this frame - has to be a separate frame from the zoom
		// press (see comment above).
		cmd->buttons &= ~IN_ATTACK2;
		cmd->buttons |= IN_ATTACK;
		s_state = FastXbowState::Idle;
		break;
	}
}
}

/*
============
KB_ConvertString

Removes references to +use and replaces them with the keyname in the output string.  If
 a binding is unfound, then the original text is retained.
NOTE:  Only works for text with +word in it.
============
*/
int KB_ConvertString(char *in, char **ppout)
{
	char sz[4096];
	char binding[64];
	char *p;
	char *pOut;
	char *pEnd;
	const char *pBinding;

	if (!ppout)
		return 0;

	*ppout = NULL;
	p = in;
	pOut = sz;
	while (*p)
	{
		if (*p == '+')
		{
			pEnd = binding;
			while (*p && (isalnum(*p) || (pEnd == binding)) && ((pEnd - binding) < 63))
			{
				*pEnd++ = *p++;
			}

			*pEnd = '\0';

			pBinding = NULL;
			if (strlen(binding + 1) > 0)
			{
				// See if there is a binding for binding?
				pBinding = gEngfuncs.Key_LookupBinding(binding + 1);
			}

			if (pBinding)
			{
				*pOut++ = '[';
				pEnd = (char *)pBinding;
			}
			else
			{
				pEnd = binding;
			}

			while (*pEnd)
			{
				*pOut++ = *pEnd++;
			}

			if (pBinding)
			{
				*pOut++ = ']';
			}
		}
		else
		{
			*pOut++ = *p++;
		}
	}

	*pOut = '\0';

	int len = strlen(sz);
	pOut = (char *)malloc(len + 1);
	memcpy(pOut, sz, len + 1);
	*ppout = pOut;

	return 1;
}

/*
============
KB_Find

Allows the engine to get a kbutton_t directly ( so it can check +mlook state, etc ) for saving out to .cfg files
============
*/
struct kbutton_s CL_DLLEXPORT *KB_Find(const char *name)
{
	//	RecClFindKey(name);

	kblist_t *p;
	p = g_kbkeys;
	while (p)
	{
		if (!_stricmp(name, p->name))
			return p->pkey;

		p = p->next;
	}
	return NULL;
}

/*
============
KB_Add

Add a kbutton_t * to the list of pointers the engine can retrieve via KB_Find
============
*/
void KB_Add(const char *name, kbutton_t *pkb)
{
	kblist_t *p;
	kbutton_t *kb;

	kb = KB_Find(name);

	if (kb)
		return;

	p = (kblist_t *)malloc(sizeof(kblist_t));
	memset(p, 0, sizeof(*p));

	V_strcpy_safe(p->name, name);
	p->pkey = pkb;

	p->next = g_kbkeys;
	g_kbkeys = p;
}

/*
============
KB_Init

Add kbutton_t definitions that the engine can query if needed
============
*/
void KB_Init(void)
{
	g_kbkeys = NULL;

	KB_Add("in_graph", &in_graph);
	KB_Add("in_mlook", &in_mlook);
	KB_Add("in_jlook", &in_jlook);
}

/*
============
KB_Shutdown

Clear kblist
============
*/
void KB_Shutdown(void)
{
	kblist_t *p, *n;
	p = g_kbkeys;
	while (p)
	{
		n = p->next;
		free(p);
		p = n;
	}
	g_kbkeys = NULL;
}

/*
============
KeyDown
============
*/
void KeyDown(kbutton_t *b)
{
	int k;
	char *c;

	c = gEngfuncs.Cmd_Argv(1);
	if (c[0])
		k = atoi(c);
	else
		k = -1; // typed manually at the console for continuous down

	if (k == b->down[0] || k == b->down[1])
		return; // repeating key

	if (!b->down[0])
		b->down[0] = k;
	else if (!b->down[1])
		b->down[1] = k;
	else
	{
		gEngfuncs.Con_DPrintf("Three keys down for a button '%c' '%c' '%c'!\n", b->down[0], b->down[1], c);
		return;
	}

	if (b->state & 1)
		return; // still down
	b->state |= 1 + 2; // down + impulse down
}

/*
============
KeyUp
============
*/
void KeyUp(kbutton_t *b)
{
	int k;
	char *c;

	c = gEngfuncs.Cmd_Argv(1);
	if (c[0])
		k = atoi(c);
	else
	{ // typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->state = 4; // impulse up
		return;
	}

	if (b->down[0] == k)
		b->down[0] = 0;
	else if (b->down[1] == k)
		b->down[1] = 0;
	else
		return; // key up without coresponding down (menu pass through)
	if (b->down[0] || b->down[1])
	{
		//Con_Printf ("Keys down for button: '%c' '%c' '%c' (%d,%d,%d)!\n", b->down[0], b->down[1], c, b->down[0], b->down[1], c);
		return; // some other key is still holding it down
	}

	if (!(b->state & 1))
		return; // still up (this should not happen)

	b->state &= ~1; // now up
	b->state |= 4; // impulse up
}

/*
============
HUD_Key_Event

Return 1 to allow engine to process the key, otherwise, act on it as needed
============
*/
int CL_DLLEXPORT HUD_Key_Event(int down, int keynum, const char *pszCurrentBinding)
{
	//	RecClKeyEvent(down, keynum, pszCurrentBinding);

	if (g_pViewport && !g_pViewport->KeyInput(down, keynum, pszCurrentBinding))
		return 0;

	if (down && CHudMenu::Get()->OnKeyPressed(keynum))
		return 0;

	return 1;
}

void IN_BreakDown(void) { KeyDown(&in_break); };
void IN_BreakUp(void) { KeyUp(&in_break); };
void IN_KLookDown(void) { KeyDown(&in_klook); }
void IN_KLookUp(void) { KeyUp(&in_klook); }
void IN_JLookDown(void) { KeyDown(&in_jlook); }
void IN_JLookUp(void) { KeyUp(&in_jlook); }
void IN_MLookDown(void) { KeyDown(&in_mlook); }
void IN_UpDown(void) { KeyDown(&in_up); }
void IN_UpUp(void) { KeyUp(&in_up); }
void IN_DownDown(void) { KeyDown(&in_down); }
void IN_DownUp(void) { KeyUp(&in_down); }
void IN_LeftDown(void) { KeyDown(&in_left); }
void IN_LeftUp(void) { KeyUp(&in_left); }
void IN_RightDown(void) { KeyDown(&in_right); }
void IN_RightUp(void) { KeyUp(&in_right); }
void IN_DucktapUp(void) { KeyUp(&in_ducktap); }
void IN_DucktapDown(void) { KeyDown(&in_ducktap); }

void IN_ForwardDown(void)
{
	KeyDown(&in_forward);
	CHudSpectator::Get()->HandleButtonsDown(IN_FORWARD);
	g_nForwardPriority = 1;
}

void IN_ForwardUp(void)
{
	KeyUp(&in_forward);
	CHudSpectator::Get()->HandleButtonsUp(IN_FORWARD);
}

void IN_BackDown(void)
{
	KeyDown(&in_back);
	CHudSpectator::Get()->HandleButtonsDown(IN_BACK);
        g_nForwardPriority = -1;
}

void IN_BackUp(void)
{
	KeyUp(&in_back);
	CHudSpectator::Get()->HandleButtonsUp(IN_BACK);
}
void IN_LookupDown(void) { KeyDown(&in_lookup); }
void IN_LookupUp(void) { KeyUp(&in_lookup); }
void IN_LookdownDown(void) { KeyDown(&in_lookdown); }
void IN_LookdownUp(void) { KeyUp(&in_lookdown); }

void IN_MoveleftDown(void)
{
    KeyDown(&in_moveleft);
    g_nSidePriority = -1;
}

void IN_MoveleftUp(void)
{
    KeyUp(&in_moveleft);
}

void IN_MoverightDown(void)
{
    KeyDown(&in_moveright);
    g_nSidePriority = 1;
}

void IN_MoverightUp(void)
{
    KeyUp(&in_moveright);
}

void IN_SpeedDown(void) { KeyDown(&in_speed); }
void IN_SpeedUp(void) { KeyUp(&in_speed); }
void IN_StrafeDown(void) { KeyDown(&in_strafe); }
void IN_StrafeUp(void) { KeyUp(&in_strafe); }

// needs capture by hud/vgui also
extern void __CmdFunc_InputPlayerSpecial(void);

void IN_Attack2Down(void)
{
	KeyDown(&in_attack2);

#ifdef _TFC
	__CmdFunc_InputPlayerSpecial();
#endif

	CHudSpectator::Get()->HandleButtonsDown(IN_ATTACK2);
}

void IN_Attack2Up(void) { KeyUp(&in_attack2); }
void IN_UseDown(void)
{
	KeyDown(&in_use);
	CHudSpectator::Get()->HandleButtonsDown(IN_USE);
}
void IN_UseUp(void) { KeyUp(&in_use); }
void IN_JumpDown(void)
{
	KeyDown(&in_jump);
	CHudSpectator::Get()->HandleButtonsDown(IN_JUMP);
}
void IN_JumpUp(void) { KeyUp(&in_jump); }
void IN_LongJumpDown(void) { KeyDown(&in_longjump); }
void IN_LongJumpUp(void) { KeyUp(&in_longjump); }
void IN_DuckDown(void)
{
	KeyDown(&in_duck);
	CHudSpectator::Get()->HandleButtonsDown(IN_DUCK);
}
void IN_DuckUp(void) { KeyUp(&in_duck); }
void IN_ReloadDown(void) { KeyDown(&in_reload); }
void IN_ReloadUp(void) { KeyUp(&in_reload); }
void IN_Alt1Down(void) { KeyDown(&in_alt1); }
void IN_Alt1Up(void) { KeyUp(&in_alt1); }
void IN_GraphDown(void) { KeyDown(&in_graph); }
void IN_GraphUp(void) { KeyUp(&in_graph); }

void IN_AttackDown(void)
{
	KeyDown(&in_attack);
	CHudSpectator::Get()->HandleButtonsDown(IN_ATTACK);
}

void IN_AttackUp(void)
{
	KeyUp(&in_attack);
	in_cancel = 0;
}

// Special handling
void IN_Cancel(void)
{
	in_cancel = 1;
}

void IN_Impulse(void)
{
	in_impulse = atoi(gEngfuncs.Cmd_Argv(1));
}

void IN_ScoreDown(void)
{
	KeyDown(&in_score);
	if (g_pViewport)
	{
		g_pViewport->ShowScoreBoard();
	}
}

void IN_ScoreUp(void)
{
	KeyUp(&in_score);
	if (g_pViewport)
	{
		g_pViewport->HideScoreBoard();
	}
}

void IN_MLookUp(void)
{
	KeyUp(&in_mlook);
	if (!(in_mlook.state & 1) && lookspring->value)
	{
		V_StartPitchDrift();
	}
}

/*
===============
CL_KeyState

Returns 0.25 if a key was pressed and released during the frame,
0.5 if it was pressed and held
0 if held then released, and
1.0 if held for the entire time
===============
*/
float CL_KeyState(kbutton_t *key)
{
	float val = 0.0;
	int impulsedown, impulseup, down;

	impulsedown = key->state & 2;
	impulseup = key->state & 4;
	down = key->state & 1;

	if (impulsedown && !impulseup)
	{
		// pressed and held this frame?
		val = down ? 0.5 : 0.0;
	}

	if (impulseup && !impulsedown)
	{
		// released this frame?
		val = down ? 0.0 : 0.0;
	}

	if (!impulsedown && !impulseup)
	{
		// held the entire frame?
		val = down ? 1.0 : 0.0;
	}

	if (impulsedown && impulseup)
	{
		if (down)
		{
			// released and re-pressed this frame
			val = 0.75;
		}
		else
		{
			// pressed and released this frame
			val = 0.25;
		}
	}

	// clear impulses
	key->state &= 1;
	return val;
}

/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles(float frametime, float *viewangles)
{
	float speed;
	float up, down;

	if (in_speed.state & 1)
	{
		speed = frametime * cl_anglespeedkey->value;
	}
	else
	{
		speed = frametime;
	}

	if (!(in_strafe.state & 1))
	{
		viewangles[YAW] -= speed * cl_yawspeed->value * CL_KeyState(&in_right);
		viewangles[YAW] += speed * cl_yawspeed->value * CL_KeyState(&in_left);
		viewangles[YAW] = anglemod(viewangles[YAW]);
	}
	if (in_klook.state & 1)
	{
		V_StopPitchDrift();
		viewangles[PITCH] -= speed * cl_pitchspeed->value * CL_KeyState(&in_forward);
		viewangles[PITCH] += speed * cl_pitchspeed->value * CL_KeyState(&in_back);
	}

	up = CL_KeyState(&in_lookup);
	down = CL_KeyState(&in_lookdown);

	viewangles[PITCH] -= speed * cl_pitchspeed->value * up;
	viewangles[PITCH] += speed * cl_pitchspeed->value * down;

	if (up || down)
		V_StopPitchDrift();

	if (viewangles[PITCH] > cl_pitchdown->value)
		viewangles[PITCH] = cl_pitchdown->value;
	if (viewangles[PITCH] < -cl_pitchup->value)
		viewangles[PITCH] = -cl_pitchup->value;

	if (viewangles[ROLL] > 50)
		viewangles[ROLL] = 50;
	if (viewangles[ROLL] < -50)
		viewangles[ROLL] = -50;
}

// =================================================================================
// JUMPBUG
// =================================================================================
void JumpBug(float frametime, usercmd_t *cmd)
{
	extern playermove_t *pmove;
 
	if (!pmove)
		return;
 
	if (pmove->movetype != MOVETYPE_WALK || pmove->waterlevel >= 2)
		return;
 
	if (pmove->onground != -1)
		return;
 
	bool bCloseToGround = false;
 
	if (pmove->velocity[2] <= 180.0f)
	{
		int savedHull = pmove->usehull;
		pmove->usehull = 0;
 
		Vector start = pmove->origin;
		Vector end = start;
		end[2] -= 2.0f; // Note: You can switch this to the velocity math later if you want 10/10
 
		// Just call the trace directly. The engine's world list is "good enough".
		pmtrace_t tr = pmove->PM_PlayerTrace(start, end, PM_NORMAL, -1);
 
		pmove->usehull = savedHull;
 
		bCloseToGround = (tr.fraction < 1.0f) && (tr.plane.normal[2] >= 0.7f);
	}
 
	if (bCloseToGround)
	{
		cmd->buttons &= ~IN_DUCK;
		cmd->buttons |= IN_JUMP;
	}
	else
	{
		cmd->buttons |= IN_DUCK;
		cmd->buttons &= ~IN_JUMP;
	}
}

/*
================
CL_CreateMove

Send the intended movement message to the server
if active == 1 then we are 1) not playing back demos ( where our commands are ignored ) and
2 ) we have finished signing on to server
================
*/
void CL_DLLEXPORT CL_CreateMove(float frametime, struct usercmd_s *cmd, int active)
{
	//	RecClCL_CreateMove(frametime, cmd, active);

	float spd;
	Vector viewangles;
	static Vector oldangles;

	if (active)
	{
		//memset( viewangles, 0, sizeof( Vector ) );
		//viewangles[ 0 ] = viewangles[ 1 ] = viewangles[ 2 ] = 0.0;
		gEngfuncs.GetViewAngles((float *)viewangles);

		CL_AdjustAngles(frametime, viewangles);

		memset(cmd, 0, sizeof(*cmd));

		gEngfuncs.SetViewAngles((float *)viewangles);

		if (in_strafe.state & 1)
		{
			cmd->sidemove += cl_sidespeed->value * CL_KeyState(&in_right);
			cmd->sidemove -= cl_sidespeed->value * CL_KeyState(&in_left);
		}

		if (CL_KeyState(&in_moveleft) && CL_KeyState(&in_moveright))
		{
			cmd->sidemove += (g_nSidePriority > 0) ? cl_sidespeed->value : -cl_sidespeed->value;
		}
		else
		{
			cmd->sidemove += cl_sidespeed->value * CL_KeyState(&in_moveright);
			cmd->sidemove -= cl_sidespeed->value * CL_KeyState(&in_moveleft);
		}

		// simulate moveup underwater for +bhop, +ljump and +jump actions
		cmd->upmove += cl_upspeed->value * std::max(CL_KeyState(&in_up), PM_GetWaterLevel() >= 2 ? max(CL_KeyState(&in_jump), CL_KeyState(&in_longjump)) : 0);
		cmd->upmove -= cl_upspeed->value * CL_KeyState(&in_down);

		if (!(in_klook.state & 1))
		{
			if (CL_KeyState(&in_forward) && CL_KeyState(&in_back))
			{
				// Both are down: Move based on priority
				cmd->forwardmove += (g_nForwardPriority > 0) ? cl_forwardspeed->value : -cl_backspeed->value;
			}
			else
			{
				// Standard behavior
				cmd->forwardmove += cl_forwardspeed->value * CL_KeyState(&in_forward);
				cmd->forwardmove -= cl_backspeed->value * CL_KeyState(&in_back);
			}
		}

		// adjust for speed key
		if (in_speed.state & 1)
		{
			cmd->forwardmove *= cl_movespeedkey->value;
			cmd->sidemove *= cl_movespeedkey->value;
			cmd->upmove *= cl_movespeedkey->value;
		}

		// clip to maxspeed
		spd = gEngfuncs.GetClientMaxspeed();
		if (spd != 0.0)
		{
			// scale the 3 speeds so that the total velocity is not > cl.maxspeed
			float fmov = sqrt((cmd->forwardmove * cmd->forwardmove) + (cmd->sidemove * cmd->sidemove) + (cmd->upmove * cmd->upmove));

			if (fmov > spd)
			{
				float fratio = spd / fmov;
				cmd->forwardmove *= fratio;
				cmd->sidemove *= fratio;
				cmd->upmove *= fratio;
			}
		}

		// Allow mice and other controllers to add their inputs
		IN_Move(frametime, cmd);

		extern playermove_t *pmove;
		if (pmove && pmove->movetype == MOVETYPE_WALK && pmove->waterlevel < 2)
		{
			bool bChangingSides = CL_KeyState(&in_moveleft) && CL_KeyState(&in_moveright);
			bool bChangingForwardBack = CL_KeyState(&in_forward) && CL_KeyState(&in_back);

			if (bChangingSides || bChangingForwardBack)
			{
				float fCurrentPlanarSpeed = sqrt((pmove->velocity[0] * pmove->velocity[0]) + (pmove->velocity[1] * pmove->velocity[1]));

				if (fCurrentPlanarSpeed < 320.0f && fCurrentPlanarSpeed > 0.1f)
				{
					float fSpeedScaleRatio = 320.0f / fCurrentPlanarSpeed;
					pmove->velocity[0] *= fSpeedScaleRatio;
					pmove->velocity[1] *= fSpeedScaleRatio;
				}
			}
		}
	}

	cmd->impulse = in_impulse;
	in_impulse = 0;

	cmd->weaponselect = g_weaponselect;
	g_weaponselect = 0;
	//
	// set button and flag bits
	//
	cmd->buttons = CL_ButtonBits(1);

	if (cl_autojump_priority.GetBool())
		autofuncs::HandleAutojump(cmd);

	if (in_ducktap.state & 1)
	{
		cmd->buttons |= IN_DUCK;
		autofuncs::HandleDucktap(cmd); // Ducktap takes priority over autojump
	}
	else if (!cl_autojump_priority.GetBool())
	{
		autofuncs::HandleAutojump(cmd);
	}

	// Using joystick?
	if (in_joystick->value)
	{
		if (cmd->forwardmove > 0)
		{
			cmd->buttons |= IN_FORWARD;
		}
		else if (cmd->forwardmove < 0)
		{
			cmd->buttons |= IN_BACK;
		}
	}

        // =================================================================================
	// JUMPBUG
	// =================================================================================
        if (g_bJumpbugActive)
        {
        JumpBug(frametime, cmd);
        }

	// =================================================================================
	// cl_fastxbow
	// =================================================================================
	autofuncs::HandleFastXbow(cmd);

	gEngfuncs.GetViewAngles((float *)viewangles);
	// Set current view angles.

	if (g_iAlive)
	{
		VectorCopy(viewangles, cmd->viewangles);
		VectorCopy(viewangles, oldangles);
	}
	else
	{
		VectorCopy(oldangles, cmd->viewangles);
	}

        // =================================================================================
        // SGS (STAND-UP GROUND STRAFE)
        // =================================================================================
        if (g_bSGSActive)
        {
            static int s_state = 0;
            static int s_frame_count = 0;
            static int s_target_frames = 0;
            static int s_cycle_count = 0;
            static int s_target_cycles = 3;
            static int s_pause_frames = 0;
            static int s_seq_index = 0;

            static const int HOLD_SEQ[] = { 3, 4, 3, 5, 3, 4 };
            static const int GAP_SEQ[]  = { 2, 6, 2, 5, 3, 5 };
            static const int SEQ_SIZE = 6;

            if (PM_GetOnGround())
            {
                if (s_pause_frames > 0)
                {
                    cmd->buttons &= ~IN_DUCK;
                    s_pause_frames--;
                    s_state = 0;
                    s_frame_count = 0;
                }
                else
                {
                    s_frame_count++;

                    if (s_state == 0)
                    {
                        cmd->buttons |= IN_DUCK;
                        s_state = 1;
                        s_frame_count = 0;
                        s_target_frames = HOLD_SEQ[s_seq_index]; 
                    }
                    else if (s_state == 1)
                    {
                        cmd->buttons |= IN_DUCK;
                        if (s_frame_count >= s_target_frames)
                        {
                            cmd->buttons &= ~IN_DUCK;
                            s_state = 2;
                            s_frame_count = 0;
                            s_target_frames = GAP_SEQ[s_seq_index]; 
                        }
                    }
                    else if (s_state == 2)
                    {
                        cmd->buttons &= ~IN_DUCK;
                        if (s_frame_count >= s_target_frames)
                        {
                            s_state = 0;
                            s_frame_count = 0;
                            s_seq_index = (s_seq_index + 1) % SEQ_SIZE;
                            s_cycle_count++;

                            if (s_cycle_count >= s_target_cycles)
                            {
                                s_cycle_count = 0;
                                s_target_cycles = 3 + (rand() % 3);
                                s_pause_frames = 6 + (rand() % 6);
                            }
                        }
                    }
                }
            }
            else
            {
                cmd->buttons &= ~IN_DUCK;
                s_state = 0;
                s_frame_count = 0;
                s_pause_frames = 0;
                s_cycle_count = 0;
            }
        }
}

/*
============
CL_IsDead

Returns 1 if health is <= 0
============
*/
int CL_IsDead(void)
{
	return (CHudHealth::Get()->m_iHealth <= 0) ? 1 : 0;
}

/*
============
CL_ButtonBits

Returns appropriate button info for keyboard and mouse state
Set bResetState to 1 to clear old state info
============
*/
int CL_ButtonBits(int bResetState)
{
	int bits = 0;

	if (in_attack.state & 3)
	{
		bits |= IN_ATTACK;
	}

	if (in_duck.state & 3)
	{
		bits |= IN_DUCK;
	}

	if (in_jump.state & 3)
	{
		if (cl_jumptype->value == 0.0 || g_iUser1 || cl_autojump.GetBool()) // Simple jump in spectator
		{
			bits |= IN_JUMP;
		}
		else
		{
			if (PM_GetOnGround())
			{
				if (!g_bDecentJumped)
				{
					bits |= IN_JUMP;
				}
				if (bResetState)
				{
					g_bDecentJumped = true;
				}
			}
			else
			{
				if (PM_GetWaterLevel() == 2)
				{
					// Over the water, glide on the surface, but only if we are over deep water
					bits |= IN_JUMP;
				}
			}
		}
	}
	else
	{
		g_bDecentJumped = false;
	}

	if (in_longjump.state & 3)
	{
		if (PM_GetOnGround() && !g_bLongJumped)
		{
			bits |= IN_DUCK | IN_JUMP;
			if (bResetState)
			{
				g_bLongJumped = true;
			}
		}
		else if (!PM_GetOnGround())
		{
			// Do not allow long jump to trigger an additional jump while in the air
			bits |= IN_DUCK;
			g_bLongJumped = true;
		}
		else
		{
			if (PM_GetWaterLevel() == 2)
			{
				// Over the water, glide on the surface, but only if we are over deep water
				bits |= IN_JUMP;
			}
			else if (PM_GetWaterLevel() < 2)
			{
				bits |= IN_DUCK;
			}
		}
	}
	else
	{
		if (bResetState)
		{
			g_bLongJumped = false;
		}
	}

	if (in_forward.state & 3)
	{
		bits |= IN_FORWARD;
	}

	if (in_back.state & 3)
	{
		bits |= IN_BACK;
	}

	if (in_use.state & 3)
	{
		bits |= IN_USE;
	}

	if (in_cancel)
	{
		bits |= IN_CANCEL;
	}

	if (in_left.state & 3)
	{
		bits |= IN_LEFT;
	}

	if (in_right.state & 3)
	{
		bits |= IN_RIGHT;
	}

	if (in_moveleft.state & 3)
	{
		bits |= IN_MOVELEFT;
	}

	if (in_moveright.state & 3)
	{
		bits |= IN_MOVERIGHT;
	}

	if (in_attack2.state & 3)
	{
		bits |= IN_ATTACK2;
	}

	if (in_reload.state & 3)
	{
		bits |= IN_RELOAD;
	}

	if (in_alt1.state & 3)
	{
		bits |= IN_ALT1;
	}

	if (in_score.state & 3)
	{
		bits |= IN_SCORE;
	}

	// Dead or in intermission? Shore scoreboard, too
	if (CL_IsDead() || gHUD.m_iIntermission)
	{
		bits |= IN_SCORE;
	}

	if (bResetState)
	{
		in_attack.state &= ~2;
		in_duck.state &= ~2;
		in_jump.state &= ~2;
		in_longjump.state &= ~2;
		in_forward.state &= ~2;
		in_back.state &= ~2;
		in_use.state &= ~2;
		in_left.state &= ~2;
		in_right.state &= ~2;
		in_moveleft.state &= ~2;
		in_moveright.state &= ~2;
		in_attack2.state &= ~2;
		in_reload.state &= ~2;
		in_alt1.state &= ~2;
		in_score.state &= ~2;
		in_ducktap.state &= ~2;
	}

	return bits;
}

/*
============
CL_ResetButtonBits

============
*/
void CL_ResetButtonBits(int bits)
{
	int bitsNew = CL_ButtonBits(0) ^ bits;

	// Has the attack button been changed
	if (bitsNew & IN_ATTACK)
	{
		// Was it pressed? or let go?
		if (bits & IN_ATTACK)
		{
			KeyDown(&in_attack);
		}
		else
		{
			// totally clear state
			in_attack.state &= ~7;
		}
	}
}

/*
============
InitInput
============
*/
void InitInput(void)
{
	gEngfuncs.pfnAddCommand("+moveup", IN_UpDown);
	gEngfuncs.pfnAddCommand("-moveup", IN_UpUp);
	gEngfuncs.pfnAddCommand("+movedown", IN_DownDown);
	gEngfuncs.pfnAddCommand("-movedown", IN_DownUp);
	gEngfuncs.pfnAddCommand("+left", IN_LeftDown);
	gEngfuncs.pfnAddCommand("-left", IN_LeftUp);
	gEngfuncs.pfnAddCommand("+right", IN_RightDown);
	gEngfuncs.pfnAddCommand("-right", IN_RightUp);
	gEngfuncs.pfnAddCommand("+forward", IN_ForwardDown);
	gEngfuncs.pfnAddCommand("-forward", IN_ForwardUp);
	gEngfuncs.pfnAddCommand("+back", IN_BackDown);
	gEngfuncs.pfnAddCommand("-back", IN_BackUp);
	gEngfuncs.pfnAddCommand("+lookup", IN_LookupDown);
	gEngfuncs.pfnAddCommand("-lookup", IN_LookupUp);
	gEngfuncs.pfnAddCommand("+lookdown", IN_LookdownDown);
	gEngfuncs.pfnAddCommand("-lookdown", IN_LookdownUp);
	gEngfuncs.pfnAddCommand("+strafe", IN_StrafeDown);
	gEngfuncs.pfnAddCommand("-strafe", IN_StrafeUp);
	gEngfuncs.pfnAddCommand("+moveleft", IN_MoveleftDown);
	gEngfuncs.pfnAddCommand("-moveleft", IN_MoveleftUp);
	gEngfuncs.pfnAddCommand("+moveright", IN_MoverightDown);
	gEngfuncs.pfnAddCommand("-moveright", IN_MoverightUp);
	gEngfuncs.pfnAddCommand("+speed", IN_SpeedDown);
	gEngfuncs.pfnAddCommand("-speed", IN_SpeedUp);
	gEngfuncs.pfnAddCommand("+attack", IN_AttackDown);
	gEngfuncs.pfnAddCommand("-attack", IN_AttackUp);
	gEngfuncs.pfnAddCommand("+attack2", IN_Attack2Down);
	gEngfuncs.pfnAddCommand("-attack2", IN_Attack2Up);
	gEngfuncs.pfnAddCommand("+use", IN_UseDown);
	gEngfuncs.pfnAddCommand("-use", IN_UseUp);
	gEngfuncs.pfnAddCommand("+jump", IN_JumpDown);
	gEngfuncs.pfnAddCommand("-jump", IN_JumpUp);
	gEngfuncs.pfnAddCommand("+ljump", IN_LongJumpDown);
	gEngfuncs.pfnAddCommand("-ljump", IN_LongJumpUp);
	gEngfuncs.pfnAddCommand("impulse", IN_Impulse);
	gEngfuncs.pfnAddCommand("+klook", IN_KLookDown);
	gEngfuncs.pfnAddCommand("-klook", IN_KLookUp);
	gEngfuncs.pfnAddCommand("+mlook", IN_MLookDown);
	gEngfuncs.pfnAddCommand("-mlook", IN_MLookUp);
	gEngfuncs.pfnAddCommand("+jlook", IN_JLookDown);
	gEngfuncs.pfnAddCommand("-jlook", IN_JLookUp);
	gEngfuncs.pfnAddCommand("+duck", IN_DuckDown);
	gEngfuncs.pfnAddCommand("-duck", IN_DuckUp);
	gEngfuncs.pfnAddCommand("+reload", IN_ReloadDown);
	gEngfuncs.pfnAddCommand("-reload", IN_ReloadUp);
	gEngfuncs.pfnAddCommand("+alt1", IN_Alt1Down);
	gEngfuncs.pfnAddCommand("-alt1", IN_Alt1Up);
	gEngfuncs.pfnAddCommand("+score", IN_ScoreDown);
	gEngfuncs.pfnAddCommand("-score", IN_ScoreUp);
	gEngfuncs.pfnAddCommand("+showscores", IN_ScoreDown);
	gEngfuncs.pfnAddCommand("-showscores", IN_ScoreUp);
	gEngfuncs.pfnAddCommand("+graph", IN_GraphDown);
	gEngfuncs.pfnAddCommand("-graph", IN_GraphUp);
	gEngfuncs.pfnAddCommand("+break", IN_BreakDown);
	gEngfuncs.pfnAddCommand("-break", IN_BreakUp);
	gEngfuncs.pfnAddCommand("+ducktap", IN_DucktapDown);
	gEngfuncs.pfnAddCommand("-ducktap", IN_DucktapUp);
        gEngfuncs.pfnAddCommand("+jumpbug", JumpbugOn);
	gEngfuncs.pfnAddCommand("-jumpbug", JumpbugOff);
        gEngfuncs.pfnAddCommand("+sgs", SgsOn);
        gEngfuncs.pfnAddCommand("-sgs", SgsOff);

	lookstrafe = gEngfuncs.pfnRegisterVariable("lookstrafe", "0", FCVAR_ARCHIVE);
	lookspring = gEngfuncs.pfnRegisterVariable("lookspring", "0", FCVAR_ARCHIVE);
	cl_anglespeedkey = gEngfuncs.pfnRegisterVariable("cl_anglespeedkey", "0.67", 0);
	cl_yawspeed = gEngfuncs.pfnRegisterVariable("cl_yawspeed", "210", 0);
	cl_pitchspeed = gEngfuncs.pfnRegisterVariable("cl_pitchspeed", "225", 0);
	cl_upspeed = gEngfuncs.pfnRegisterVariable("cl_upspeed", "320", 0);
	cl_forwardspeed = gEngfuncs.pfnRegisterVariable("cl_forwardspeed", "400", FCVAR_ARCHIVE);
	cl_backspeed = gEngfuncs.pfnRegisterVariable("cl_backspeed", "400", FCVAR_ARCHIVE);
	cl_sidespeed = gEngfuncs.pfnRegisterVariable("cl_sidespeed", "400", 0);
	cl_movespeedkey = gEngfuncs.pfnRegisterVariable("cl_movespeedkey", "0.3", 0);
	cl_pitchup = gEngfuncs.pfnRegisterVariable("cl_pitchup", "89", 0);
	cl_pitchdown = gEngfuncs.pfnRegisterVariable("cl_pitchdown", "89", 0);

	cl_vsmoothing = gEngfuncs.pfnRegisterVariable("cl_vsmoothing", "0.05", FCVAR_ARCHIVE);
	cl_jumptype = gEngfuncs.pfnRegisterVariable("cl_jumptype", "1", FCVAR_ARCHIVE);

	m_pitch = gEngfuncs.pfnRegisterVariable("m_pitch", "0.022", FCVAR_ARCHIVE);
	m_yaw = gEngfuncs.pfnRegisterVariable("m_yaw", "0.022", FCVAR_ARCHIVE);
	m_forward = gEngfuncs.pfnRegisterVariable("m_forward", "1", FCVAR_ARCHIVE);
	m_side = gEngfuncs.pfnRegisterVariable("m_side", "0.8", FCVAR_ARCHIVE);

	// Initialize third person camera controls.
	CAM_Init();
	// Initialize inputs
	IN_Init();
	// Initialize keyboard
	KB_Init();
	// Initialize view system
	V_Init();
}

/*
============
ShutdownInput
============
*/
void ShutdownInput(void)
{
	IN_Shutdown();
	KB_Shutdown();
}
