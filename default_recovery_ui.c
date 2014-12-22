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

#include <linux/input.h>

#include "common.h"
#include "extendedcommands.h"
#include "recovery_ui.h"

#define resX gr_fb_width()		//Value obtained from function 'gr_fb_width()'
#define resY gr_fb_height()		//Value obtained from function 'gr_fb_height()'	

int touchY = 0;

char* MENU_HEADERS[] = { NULL };

char* MENU_ITEMS[] = { "Reboot Phone",
                       "Install zip",
                       "Wipe Menu",
                       "Backup/Restore",
                       "Mounts/Storage",
                       "Advanced Menu",
                       "Carliv Menu",
                       "Power Menu",
                       NULL };

void device_ui_init(UIParameters* ui_parameters) {
}

int device_recovery_start() {
    return 0;
}

// add here any key combo check to reboot device
int device_reboot_now(volatile char* key_pressed, int key_code) {
    return 0;
}

int device_perform_action(int which) {
    return which;
}


int device_wipe_data() {
    return 0;
}

int device_wipe_cache() {
    return 0;
}

int device_wipe_dalvik_cache() {
    return 0;
}

int MENU_MAX_HEIGHT()
{
	return gr_get_height(gMenuIco[MENU_SELECT]);
}

int get_menu_icon_info(int indx1, int indx2) {
//ToDo: Following switch case should be replaced by array or structure

int caseN = indx1*4 + indx2;

switch (caseN) {
	case 0:
		return 1*resX/8;
	case 1:
		return (resY - MENU_MAX_HEIGHT()/2);
	case 2:
		return 0*resX/4;
	case 3:
		return 1*resX/4;
	case 4:
		return 3*resX/8;
	case 5:
		return (resY - MENU_MAX_HEIGHT()/2);
	case 6:
		return 1*resX/4;
	case 7:
		return 2*resX/4;
	case 8:
		return 5*resX/8;
	case 9:
		return (resY - MENU_MAX_HEIGHT()/2);
	case 10:
		return 2*resX/4;
	case 11:
		return 3*resX/4;
	case 12:
		return 7*resX/8;
	case 13:
		return (resY - MENU_MAX_HEIGHT()/2);
	case 14:
		return 3*resX/4;
	case 15:
		return 4*resX/4;

}

return 0;
}

//For those devices which has skewed X axis and Y axis detection limit (Not similar to XY resolution of device), So need normalization
int MT_X(int fd, int x)
{
	int abs_store[6] = {0};

  	ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), abs_store);
   	int maxX = abs_store[2];

	int out;
	out = maxX ? (int)((float)x*gr_fb_width()/maxX) : x;

	return out;
}

int MT_Y(int fd, int y)
{	
	int abs_store[6] = {0};
   	
   	ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), abs_store);
   	int maxY = abs_store[2];

	int out;
	out = maxY ? (int)((float)y*gr_fb_height()/maxY) : y;

	return out;
}
