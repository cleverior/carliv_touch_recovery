**Version 2.7**
This is a modified **full touch** version of **CWM Recovery 6.0.3.7** from cm-10.1 branch. Use it with CM-10.1 building environment.
My main goal was to bring up a light but features rich recovery with touch support. All the other existent touch recoveries (TWRP, PhilZ, Miui) are just too big for the very small recovery partition that we find in most of MTK phones.
____

This version was made for MTK powered phones with **UBIFS** file system. It is still not very well tested, because I don't have a phone like that, and there  weren't many people willing to test it. Don't use it if you don't have such a phone, use the regular version instead.
____

Some of the features:
- *Full Touch module* originally developed by [Napstar-xda](https://github.com/Napstar-xda/android_bootable_recovery/tree/cm-10.1) from [UtterChaos Team](http://forum.xda-developers.com/showthread.php?t=1485829), using as inspiration the touch module developed by [sztupy](https://github.com/sztupy) for [SteamMOD](https://github.com/SteamMOD);
- *Aroma File Manager support*, ported from [sk8erwitskil recovery](https://github.com/sk8erwitskil);
- *Automatic get mtk partitions size*, ported from [PhilZ Touch recovery](https://github.com/PhilZ-cwm6/philz_touch_cwm6) who ported it from [Dees-Troy - TWRP](https://github.com/TeamWin/Team-Win-Recovery-Project);
- Separate *wipe menu*;
- Separate *power menu*;
- for MTK phones separate *backup/restore Nvram menu*;
- *Advanced backup/restore menu*;
- *Tar.gz backup format* (compressed);
- Rainbow UI enabler menu;
- unique GUI;
- and some other;
- some of the features are inspired from [ProjectOpenCannibal Recovery](https://github.com/ProjectOpenCannibal/android_bootable_recovery);

____

The UBIFS support was originally developed by [christiantroy - Alan Marchesan](https://github.com/christiantroy), but I changed some things after I studied the original stock MTK recovery with UBIFS support.
____

To build, do it like with any other CWM recovery, but it is important to set up a flag in BoardConfig for your device screen resolution `DEVICE_RESOLUTION := 720x1280` and use yours - 720x1280 here is just for example. It won't work without it, because that will add specific screen size images for menu buttons. The possible resolution to set are: 240x240, 320x480, 480x800, 480x854, 540x960, 600x1024, 720x1280, 768x1024, 768x1280, 800x1200, 800x1280, 1024x600, 1080x1920, 1280x720, 1280x768. If your screen width is not in the list, you have to make it, otherwise it won't work.
Also you can choose a font that will look better on your screen, since now with xiaolu's courtesy we have more: `BOARD_USE_CUSTOM_RECOVERY_FONT := \"font_17x33.h\"`.
____

For MTK phones, there is need of another flag in BoardConfig `BOARD_HAS_MTK := true` to activate MTK specific functions. And a different flag for system files `TARGET_USERIMAGES_USE_UBIFS := true`.
____

You don't need to change anything to add your credits in build, because the code will add it from your computer with shell, like in building kernels: it will print on screen "**Compiled by yourusername@yourhostname on: date**".

Enjoy!
