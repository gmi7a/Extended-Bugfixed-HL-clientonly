This project is based on tmp64's BugfixedHL-Rebased project (https://github.com/tmp64/BugfixedHL-Rebased/)

- Removed Bunnyhop speed cap entirely (cl_bhopcap still exists but is useless now), Bunnyhopping now in servers with uncapped bunnyhop speed eliminates the micro delay caused by the client's PM_PreventMegaBunnyJumping function inside pm_shared.cpp, like the OpenAG Client (https://github.com/YaLTeR/OpenAG)

- Added +jumpbug, hold to the key bound to +jumpbug, ducks midair - automatically unducks and jumps in the exact 2 units above the ground eliminating fall damage and fall sound (if there was no horizontal velocity), it is not 100% guaranteed because for a 100% chance of triggering, host_framerate needs to be adjusted which is not possible in multiplayer, consider highering framerate if possible and increase rate - cmdrate - etc.., it has an estimated chance of 70% successrate depending on several factors.

- Added cl_fastxbow, if set to 1, it checks if you are holding the crossbow weapon, if yes, when you click LMB *Left Mouse Button* instead of firing the slow explosive arrow in multiplayer, it automatically zoomes then fires, similar to a fasxbow alias - wait script but not dependent on frame rate - binding to a button, also depends in the ping for a high success rate, but it has an estimated success rate of 90%, it is not ideal for a fast xbow snipe switch (switching to crossbow fast then firing).

- Added +sgs (well, not quite the sgs we know in cs 1.6, but a more humanized ducktapping mechanism) it is less effective than +ducktap, but it is built for servers with strict anti-cheat plugins for ducktapping, but it is extremely more effective than a mousewheel binding to +duck at maintaining momentum after a speed boost (e,g after a gauss boost)

For building: Download tmp64's BugfixedHL-Rebased's source code, paste src inside the root dir and replace conflicting files.

Note: the current release is only for windows operating systems, a linux version will be released soon, more features will be added soon aswell.

Installation: Download the release here: https://github.com/gmi7a/Extended-Bugfixed-HL-clientonly/releases/ - Place valve_addon in Steam/steamapps/common/Half-Life and replace any conflicting files, launch the game and head to video settings, Check and enable "Allow custom addon content" -> Apply -> OK.

Special Thanks to: Valve for HLSDK release - tmp64 and LevShisterov for BugfixedHL, anyone who was one of the reasons this existed.

This project was private first then i decided to release it, it is still in its early stages and i might work to add and fix more features later.
