**Version 3.0 kitkat**

This is a modified **full touch** version of **CWM Recovery 6.0.5.1** from cm-11.0 branch. Use it with CM-11.0 building environment.
My main goal was to bring up a light but features rich recovery with touch support. All the other existent touch recoveries (TWRP, PhilZ, Miui) are just too big for the very small recovery partition that we find in most of MTK phones.

____

It includes more features than original clockworkmod recovery, and despite the fact that was first made for MTK powered phones, it can be used for other phones as well.

____

Some of the features:
- *Full Touch module* originally developed by [Napstar-xda](https://github.com/Napstar-xda/android_bootable_recovery/tree/cm-10.1) from [UtterChaos Team](http://forum.xda-developers.com/showthread.php?t=1485829), using as inspiration the touch module developed by [sztupy](https://github.com/sztupy) for [SteamMOD](https://github.com/SteamMOD);
- *Aroma File Manager support*, ported from [sk8erwitskil recovery](https://github.com/sk8erwitskil) - reworked and adapted to cm-11.0;
- *Automatic get mtk partitions size*, ported from [PhilZ Touch recovery](https://github.com/PhilZ-cwm6/philz_touch_cwm6) who ported it from [Dees-Troy - TWRP](https://github.com/TeamWin/Team-Win-Recovery-Project);
- *more fonts* to choose from, all credits to [xiaolu](https://github.com/xiaolu/android_bootable_recovery) who create them;
- Separate *wipe menu*;
- Separate *power menu*;
- for MTK phones separate *backup/restore Nvram menu*;
- *Advanced backup/restore menu*;
- Rainbow UI enabler menu;
- unique GUI;
- and some other;
- some of the features are inspired from [ProjectOpenCannibal Recovery](https://github.com/ProjectOpenCannibal/android_bootable_recovery); 

____

You can check [my building from source guide](http://forum.xda-developers.com/android/development/guide-how-to-build-cwm-based-recovery-t2973804).
To build, do it like with any other CWM recovery, but it is important to set up a flag in BoardConfig for your device screen resolution `DEVICE_RESOLUTION := 720x1280` and use yours - 720x1280 here is just for example. It won't work without it, because that will add specific screen size images for menu buttons. The possible resolution to set are: 240x240, 320x480, 480x800, 480x854, 540x960, 600x1024, 720x1280, 768x1024, 768x1280, 800x1200, 800x1280, 1024x600, 1080x1920, 1280x720, 1280x768. If your screen resolution is not in the list, you have to make it, otherwise it won't work. And if you don't want to replace the stock CWM recovery folder, you can add this as "recovery-carliv" folder in "bootable" along with "recovery" and "recovery-cm". In this case you will need another flag on BoardConfig: `RECOVERY_VARIANT := carliv`.
Also you can choose a font that will look better on your screen, since now with xiaolu's courtesy we have more: `BOARD_USE_CUSTOM_RECOVERY_FONT := \"font_17x33.h\"`.

____

For MTK phones, there is need of another flag in BoardConfig `BOARD_HAS_MTK := true` to activate MTK specific functions. 

____

You don't need to change anything to add your credits in build, because the code will add it from your computer with shell, like in building kernels: it will print on screen "**Compiled by yourusername@yourhostname on: date**".

Enjoy!
