/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _RECOVERY_UI_H
#define _RECOVERY_UI_H

#include "common.h"
#include "minui/minui.h"
#include "minzip/Zip.h"

// Called before UI library is initialized.  Can change things like
// how many frames are included in various animations, etc.
extern void device_ui_init(UIParameters* ui_parameters);

// Called when recovery starts up.  Returns 0.
extern int device_recovery_start();

// Called in the input thread when a new key (key_code) is pressed.
// *key_pressed is an array of KEY_MAX+1 bytes indicating which other
// keys are already pressed.  Return true if the text display should
// be toggled.
extern int device_toggle_display(volatile char* key_pressed, int key_code);

// Called in the input thread when a new key (key_code) is pressed.
// *key_pressed is an array of KEY_MAX+1 bytes indicating which other
// keys are already pressed.  Return true if the device should reboot
// immediately.
extern int device_reboot_now(volatile char* key_pressed, int key_code);

// Called from the main thread when recovery is waiting for input and
// a key is pressed.  key is the code of the key pressed; visible is
// true if the recovery menu is being shown.  Implementations can call
// ui_key_pressed() to discover if other keys are being held down.
// Return one of the defined constants below in order to:
//
//   - move the menu highlight (HIGHLIGHT_*)
//   - invoke the highlighted item (SELECT_ITEM)
//   - do nothing (NO_ACTION)
//   - invoke a specific action (a menu position: any non-negative number)
extern int device_handle_key(int key, int visible);

// Perform a recovery action selected from the menu.  'which' will be
// the item number of the selected menu item, or a non-negative number
// returned from device_handle_key().  The menu will be hidden when
// this is called; implementations can call ui_print() to print
// information to the screen.
extern int device_perform_action(int which);

#define NO_ACTION           -1

#define HIGHLIGHT_UP        -2
#define HIGHLIGHT_DOWN      -3
#define SELECT_ITEM         -4
#define GO_BACK             -5

#define ITEM_REBOOT          0
#define ITEM_APPLY_EXT       1
#define ITEM_APPLY_SDCARD    1  // historical synonym for ITEM_APPLY_EXT
#define ITEM_APPLY_ZIP       1  // used for installing an update from a zip
#define ITEM_WIPE_MENU       2
// unused in cwr
#define ITEM_APPLY_CACHE     3
#define ITEM_NANDROID        3
#define ITEM_PARTITION       4
#define ITEM_ADVANCED        5
#define ITEM_CARLIV          6
#define ITEM_POWER           7

// Header text to display above the main menu.
extern char* MENU_HEADERS[];

// Text of menu items.
extern char* MENU_ITEMS[];

extern int device_wipe_data();

extern int device_wipe_cache();

extern int device_wipe_dalvik_cache();

// Loosely track the depth of the current menu
extern int ui_root_menu;

#define MENU_ICON_X			0
#define MENU_ICON_Y			1
#define MENU_ICON_XL		2
#define MENU_ICON_XR		3

extern int resX;
extern int resY;
extern int maxX;
extern int maxY;
extern int touchY;

int get_menu_icon_info(int indx1, int indx2);
extern gr_surface *gMenuIco;

int
get_menu_selection(char** headers, char** items, int menu_only, int initial_selection);

void
set_sdcard_update_bootloader_message();

extern int ui_handle_key(int key, int visible);

// call a clean reboot
void reboot_main_system(int cmd, int flags, char *arg);

#endif
