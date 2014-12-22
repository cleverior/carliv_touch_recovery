/*
 * Copyright (C) 2007 The Android Open Source Project
 * Copyright (C) 2014 The CyanogenMod Project
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

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>

#include "extendedcommands.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "cutils/android_reboot.h"
#include "cutils/properties.h"
#include "minui/minui.h"
#include "recovery_ui.h"
#include "voldclient/voldclient.h"

#ifndef SYN_REPORT
#define SYN_REPORT 0x00
#endif
#ifndef SYN_CONFIG
#define SYN_CONFIG 0x01
#endif
#ifndef SYN_MT_REPORT
#define SYN_MT_REPORT 0x02
#endif
#define ABS_MT_POSITION     0x2a 
#define ABS_MT_AMPLITUDE    0x2b 
#define ABS_MT_SLOT         0x2f
#define ABS_MT_TOUCH_MAJOR  0x30
#define ABS_MT_TOUCH_MINOR  0x31
#define ABS_MT_WIDTH_MAJOR  0x32
#define ABS_MT_WIDTH_MINOR  0x33
#define ABS_MT_ORIENTATION  0x34
#define ABS_MT_POSITION_X   0x35
#define ABS_MT_POSITION_Y   0x36
#define ABS_MT_TOOL_TYPE    0x37
#define ABS_MT_BLOB_ID      0x38
#define ABS_MT_TRACKING_ID  0x39
#define ABS_MT_PRESSURE     0x3a
#define ABS_MT_DISTANCE     0x3b


extern int __system(const char *command);
extern int volumes_changed();

#if defined(BOARD_HAS_NO_SELECT_BUTTON) || defined(BOARD_TOUCH_RECOVERY)
static int gShowBackButton = 1;
#else
static int gShowBackButton = 0;
#endif

#define MAX_COLS 96
#define MAX_ROWS 32
#define MENU_MAX_COLS 64
#define MENU_MAX_ROWS 250
#define MENU_ITEM_HEADER "-"
#define MENU_ITEM_HEADER_LENGTH strlen(MENU_ITEM_HEADER)

#define MIN_LOG_ROWS 3
#define MIN_BLANK_ROWS 2

#define CHAR_WIDTH BOARD_RECOVERY_CHAR_WIDTH
#define CHAR_HEIGHT BOARD_RECOVERY_CHAR_HEIGHT

// Delay in seconds to refresh clock and USB plugged volumes
#define REFRESH_TIME_USB_INTERVAL 5

#define UI_WAIT_KEY_TIMEOUT_SEC    3600
#define UI_KEY_REPEAT_INTERVAL 80
#define UI_KEY_WAIT_REPEAT 400
#define UI_MIN_PROG_DELTA_MS 200

UIParameters ui_parameters = {
    6,       // indeterminate progress bar frames
    20,      // fps
    5,       // installation icon frames (0 == static image)
    60, 190, // installation icon overlay offset
};

static pthread_mutex_t gUpdateMutex = PTHREAD_MUTEX_INITIALIZER;
static gr_surface gBackgroundIcon[NUM_BACKGROUND_ICONS];
static gr_surface gMenuIcon[NUM_MENU_ICON];
gr_surface *gMenuIco = &gMenuIcon[0];
static gr_surface *gInstallationOverlay;
static gr_surface *gProgressBarIndeterminate;
static gr_surface gProgressBarEmpty;
static gr_surface gProgressBarFill;
static gr_surface gVirtualKeys;
static gr_surface gBackground;
static int ui_has_initialized = 0;
static int ui_log_stdout = 1;
static int selMenuIcon = 0;
static int selMenuButtonIcon = -1;

static int boardEnableKeyRepeat = 0;
static int boardRepeatableKeys[64];
static int boardNumRepeatableKeys = 0;

static const struct { gr_surface* surface; const char *name; } BITMAPS[] = {
    { &gBackgroundIcon[BACKGROUND_ICON_INSTALLING], "icon_installing" },
    { &gBackgroundIcon[BACKGROUND_ICON_ERROR],      "icon_error" },
    { &gBackgroundIcon[BACKGROUND_ICON_CLOCKWORK],  "icon_clockwork" },
    { &gBackgroundIcon[BACKGROUND_ICON_FIRMWARE_INSTALLING], "icon_firmware_install" },
    { &gBackgroundIcon[BACKGROUND_ICON_FIRMWARE_ERROR], "icon_firmware_error" },
    { &gMenuIcon[MENU_BACK],     	"icon_back" },
    { &gMenuIcon[MENU_DOWN],     	"icon_down" },
    { &gMenuIcon[MENU_UP],       	"icon_up" },
    { &gMenuIcon[MENU_SELECT],   	"icon_select" },
    { &gMenuIcon[MENU_BACK_M],   	"icon_backM" },
    { &gMenuIcon[MENU_DOWN_M],   	"icon_downM" },
    { &gMenuIcon[MENU_UP_M],     	"icon_upM" },
    { &gMenuIcon[MENU_SELECT_M], 	"icon_selectM" },
    { &gMenuIcon[MENU_BUTTON_L],    	"button_L" },
	{ &gMenuIcon[MENU_BUTTON_L_SEL],	"button_L_sel" },
	{ &gMenuIcon[MENU_BUTTON_R],    	"button_R" },
	{ &gMenuIcon[MENU_BUTTON_R_SEL],	"button_R_sel" },
	{ &gMenuIcon[MENU_BUTTON_L_LOWHALF],	"button_L_Lowhalf" },
	{ &gMenuIcon[MENU_BUTTON_R_LOWHALF],	"button_R_Lowhalf" },
	{ &gMenuIcon[MENU_BUTTON_R_HALF],	"button_R_half" },
    { &gProgressBarEmpty,               "progress_empty" },
    { &gProgressBarFill,                "progress_fill" },
    { &gVirtualKeys,                    "virtual_keys" },
    { &gBackground,                "stitch" },
    { NULL,                             NULL },
};

static int gCurrentIcon = 0;
static int gInstallingFrame = 0;

static struct timeval lastprogupd = (struct timeval) {0};

static enum ProgressBarType {
    PROGRESSBAR_TYPE_NONE,
    PROGRESSBAR_TYPE_INDETERMINATE,
    PROGRESSBAR_TYPE_NORMAL,
} gProgressBarType = PROGRESSBAR_TYPE_NONE;

// Progress bar scope of current operation
static float gProgressScopeStart = 0.0;
static float gProgressScopeSize = 0.0;
static float gProgress = 0.0;
static double gProgressScopeTime;
static double gProgressScopeDuration;

// Set to 1 when both graphics pages are the same (except for the progress bar)
static int gPagesIdentical = 0;

#define MENU_HEIGHT gr_get_height(gMenuIcon[MENU_BUTTON_L])
#define MENU_CENTER (gr_get_height(gMenuIcon[MENU_BUTTON_L])/2)
#define MENU_INCREMENT (gr_get_height(gMenuIcon[MENU_BUTTON_L])/2)
#define MENU_ITEM_LEFT_OFFSET 0.03*gr_fb_width()
#define MENU_ITEM_RIGHT_OFFSET 0.52*gr_fb_width()

#define resX gr_fb_width()		
#define resY gr_fb_height()

#define MENU_MAX_HEIGHT gr_get_height(gMenuIcon[MENU_SELECT])	

#define BUTTON_MAX_ROWS (int)(0.8*resY/MENU_INCREMENT)
#define BUTTON_EQUIVALENT(x) (int)((x*CHAR_HEIGHT)/MENU_INCREMENT)

// Log text overlay, displayed when a magic key is pressed
static char text[MAX_ROWS][MAX_COLS];
static int text_cols = 0;
static int text_rows = 0;
static int text_col = 0;
static int text_row = 0;
static int text_top = 0;
static int show_text = 0;
static int show_text_ever = 0; // i.e. has show_text ever been 1?

static char menu[MENU_MAX_ROWS][MENU_MAX_COLS];
static char submenu[MENU_MAX_ROWS][MENU_MAX_COLS];
static int menuTextColor[4] = {MENU_TEXT_COLOR};
static int show_menu = 0;
static int menu_top = 0;
static int menu_items = 0;
static int menu_sel = 0;
static int menu_show_start = 0; // line at which menu display starts
static int menu_rows;
static int max_menu_rows;

static unsigned cur_rainbow_color = 0;
static int gRainbowMode = 0;

// Key event input queue
static pthread_mutex_t key_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t key_queue_cond = PTHREAD_COND_INITIALIZER;
static int key_queue[256], key_queue_len = 0, key_queue_len_back = 0;
static unsigned long key_last_repeat[KEY_MAX + 1];
static unsigned long key_press_time[KEY_MAX + 1];
static volatile char key_pressed[KEY_MAX + 1];

// Prototypes for static functions that are used before defined
static void update_screen_locked(void);
static int ui_wait_key_with_repeat();
static void ui_rainbow_mode();

// Threads
static pthread_t pt_ui_thread;
static pthread_t pt_input_thread;
static volatile int pt_ui_thread_active = 1;
static volatile int pt_input_thread_active = 1;

// Desire/Nexus and similar have 2, SGS has 5, SGT has 10, we take the max as it's cool. We'll only use 1 however
#define MAX_MT_POINTS 10

// Struct to store mouse events
static struct mousePosStruct {
  unsigned int x;
  unsigned int y;
  unsigned int pressure; // 0:up or 255:down
  unsigned int size;
  unsigned int num;
  unsigned int length; // length of the line drawn while in touch state
  unsigned int Xlength; // length of the line drawn along X axis while in touch state
} actPos, grabPos, oldMousePos[MAX_MT_POINTS], mousePos[MAX_MT_POINTS], backupPos;
//Struct to return key events to recovery.c through ui_wait_key()
volatile struct keyStruct key;

// Current time
static double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// Draw the given frame over the installation overlay animation.  The
// background is not cleared or draw with the base icon first; we
// assume that the frame already contains some other frame of the
// animation.  Does nothing if no overlay animation is defined.
// Should only be called with gUpdateMutex locked.
static void draw_install_overlay_locked(int frame) {
    if (gInstallationOverlay == NULL) return;
    gr_surface surface = gInstallationOverlay[frame];
    int iconWidth = gr_get_width(surface);
    int iconHeight = gr_get_height(surface);
    gr_blit(surface, 0, 0, iconWidth, iconHeight,
            ui_parameters.install_overlay_offset_x,
            ui_parameters.install_overlay_offset_y);
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with gUpdateMutex locked.
static void draw_background_locked(int icon) {
    gPagesIdentical = 0;

    int bw = gr_get_width(gBackground);
    int bh = gr_get_height(gBackground);
    int bx = 0;
    int by = 0;
    for (by = 0; by < gr_fb_height(); by += bh) {
        for (bx = 0; bx < gr_fb_width(); bx += bw) {
            gr_blit(gBackground, 0, 0, bw, bh, bx, by);
        }
    }

    if (icon) {
        gr_surface surface = gBackgroundIcon[icon];
        int iconWidth = gr_get_width(surface);
        int iconHeight = gr_get_height(surface);
        int iconX = (gr_fb_width() - iconWidth) / 2;
        int iconY = (gr_fb_height() - iconHeight) / 2;
        gr_blit(surface, 0, 0, iconWidth, iconHeight, iconX, iconY);
        if (icon == BACKGROUND_ICON_INSTALLING) {
            draw_install_overlay_locked(gInstallingFrame);
        }
    }
}

// Draw the currently selected icon (if any) at given location.
// Should only be called with gUpdateMutex locked.
static void draw_icon_locked(gr_surface icon,int locX, int locY)
{
    gPagesIdentical = 0;

    if (icon) {
        int iconWidth = gr_get_width(icon);
        int iconHeight = gr_get_height(icon);
        int iconX = locX - iconWidth / 2;
        int iconY = locY - iconHeight / 2;
        gr_blit(icon, 0, 0, iconWidth, iconHeight, iconX, iconY);
    }
}

static void ui_increment_frame() {
    if (!ui_has_initialized) return;
    gInstallingFrame =
        (gInstallingFrame + 1) % ui_parameters.installing_frames;
}

static long delta_milliseconds(struct timeval from, struct timeval to) {
    long delta_sec = (to.tv_sec - from.tv_sec)*1000;
    long delta_usec = (to.tv_usec - from.tv_usec)/1000;
    return (delta_sec + delta_usec);
}

// Draw the progress bar (if any) on the screen; does not flip pages
// Should only be called with gUpdateMutex locked and if ui_has_initialized is true
static void draw_progress_locked() {
    if (gCurrentIcon == BACKGROUND_ICON_INSTALLING) {
        // update the installation animation, if active
        if (ui_parameters.installing_frames > 0)
            ui_increment_frame();
        draw_install_overlay_locked(gInstallingFrame);
    }

    if (gProgressBarType != PROGRESSBAR_TYPE_NONE) {
        int iconHeight = gr_get_height(gBackgroundIcon[BACKGROUND_ICON_INSTALLING]);
        int width = gr_get_width(gProgressBarEmpty);
        int height = gr_get_height(gProgressBarEmpty);

        int dx = (gr_fb_width() - width)/2;
        int dy = (3*gr_fb_height() + iconHeight - 2*height)/4;

        // Erase behind the progress bar (in case this was a progress-only update)
        gr_color(0, 0, 0, 255);
        gr_fill(dx, dy, width, height);

        if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL) {
            float progress = gProgressScopeStart + gProgress * gProgressScopeSize;
            int pos = (int) (progress * width);

            if (pos > 0) {
                gr_blit(gProgressBarFill, 0, 0, pos, height, dx, dy);
            }
            if (pos < width-1) {
                gr_blit(gProgressBarEmpty, pos, 0, width-pos, height, dx+pos, dy);
            }
        }

        if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE) {
            static int frame = 0;
            gr_blit(gProgressBarIndeterminate[frame], 0, 0, width, height, dx, dy);
            frame = (frame + 1) % ui_parameters.indeterminate_frames;
        }
    }

    gettimeofday(&lastprogupd, NULL);
}

static void draw_virtualkeys_locked() {
    gr_surface surface = gVirtualKeys;
    int iconWidth = gr_get_width(surface);
    int iconHeight = gr_get_height(surface);
    int iconX = (gr_fb_width() - iconWidth) / 2;
    int iconY = (gr_fb_height() - iconHeight);
    gr_blit(surface, 0, 0, iconWidth, iconHeight, iconX, iconY);
}

static void draw_text_line(int row, const char* t, int rowOffset, int isMenu, int xOffset) {
  if (t[0] != '\0') {
	if (ui_get_rainbow_mode()) ui_rainbow_mode(); 
    if (isMenu == 1)
		gr_text(xOffset, rowOffset + (row+1)*MENU_INCREMENT-1+(MENU_HEIGHT/2), t, 0);
	else 
		gr_text(xOffset, rowOffset + (row+1)*CHAR_HEIGHT-1, t, 0);	
  }
}

void ui_setMenuTextColor(int r, int g, int b, int a) {
    menuTextColor[0] = r;
    menuTextColor[1] = g;
    menuTextColor[2] = b;
    menuTextColor[3] = a;
}

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_screen_locked(void) {
    if (!ui_has_initialized)
        return;
	
//ToDo: Following structure should be global
	struct { int x; int y; int xL; int xR; } MENU_ICON[] = {
		{  get_menu_icon_info(MENU_BACK,MENU_ICON_X),	get_menu_icon_info(MENU_BACK,MENU_ICON_Y), get_menu_icon_info(MENU_BACK,MENU_ICON_XL), get_menu_icon_info(MENU_BACK,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_DOWN,MENU_ICON_X),	get_menu_icon_info(MENU_DOWN,MENU_ICON_Y), get_menu_icon_info(MENU_DOWN,MENU_ICON_XL), get_menu_icon_info(MENU_DOWN,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_UP,MENU_ICON_X),	get_menu_icon_info(MENU_UP,MENU_ICON_Y), get_menu_icon_info(MENU_UP,MENU_ICON_XL), get_menu_icon_info(MENU_UP,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_SELECT,MENU_ICON_X),	get_menu_icon_info(MENU_SELECT,MENU_ICON_Y), get_menu_icon_info(MENU_SELECT,MENU_ICON_XL), get_menu_icon_info(MENU_SELECT,MENU_ICON_XR) },
	};

    draw_background_locked(gCurrentIcon);
    draw_progress_locked();

    if (show_text) {
		gr_color(0, 0, 0, 140);
        gr_fill(0, 0, gr_fb_width(), gr_fb_height());

        int total_rows = (gr_fb_height() / CHAR_HEIGHT) - MIN_BLANK_ROWS;
        int i = 0;
        int j = 0;
		int isMenu = 1;
		int rowOffset = 0;
        int row = 0;             // current row that we are drawing on
        if (show_menu) {
				draw_icon_locked(gMenuIcon[MENU_BACK], MENU_ICON[MENU_BACK].x, MENU_ICON[MENU_BACK].y );
				draw_icon_locked(gMenuIcon[MENU_DOWN], MENU_ICON[MENU_DOWN].x, MENU_ICON[MENU_DOWN].y);
				draw_icon_locked(gMenuIcon[MENU_UP], MENU_ICON[MENU_UP].x, MENU_ICON[MENU_UP].y );
				draw_icon_locked(gMenuIcon[MENU_SELECT], MENU_ICON[MENU_SELECT].x, MENU_ICON[MENU_SELECT].y );
			
			gr_color(HEADER_TEXT_COLOR);                   
            for (i = 0; i < menu_top; ++i) {
                draw_text_line(i, menu[i], rowOffset, !isMenu, 0);
                row++;
            }

            if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
                j = BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top);
            else
                j = menu_items - menu_show_start;

			rowOffset = menu_top*CHAR_HEIGHT+4-MENU_CENTER;
            
			gr_color(menuTextColor[0], menuTextColor[1], menuTextColor[2], menuTextColor[3]);
            for (i = menu_show_start + menu_top; i < (menu_show_start + menu_top + j); ++i) {
				if (i == menu_top + menu_sel) {
					if ((i - menu_top - menu_show_start)%2 == 0)
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_L_SEL], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT);
						gr_color(255, 255, 255, 255);
						if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_LEFT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_LEFT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_LEFT_OFFSET);
				                }

					}
					else
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_R_SEL], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT);
						gr_color(255, 255, 255, 255);
						if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                }
					}
                    gr_color(menuTextColor[0], menuTextColor[1], menuTextColor[2], menuTextColor[3]);                   
                } else {
					if ((i - menu_top - menu_show_start)%2 == 0)
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_L], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT);
				                gr_color(menuTextColor[0], menuTextColor[1], menuTextColor[2], menuTextColor[3]);
						if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_LEFT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_LEFT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_LEFT_OFFSET);
				                }
					}
					else
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_R], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT);
				                gr_color(menuTextColor[0], menuTextColor[1], menuTextColor[2], menuTextColor[3]);
												if(menu[i][0] != '-')
				                    draw_text_line(i - menu_show_start - menu_top , menu[i], rowOffset, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                else
				                {
				                    draw_text_line(i - menu_show_start - menu_top , menu[i]+1, rowOffset-CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                    draw_text_line(i - menu_show_start - menu_top , submenu[i], rowOffset+CHAR_HEIGHT/2+1, isMenu, MENU_ITEM_RIGHT_OFFSET);
				                }
					}
                }
                row++;
                if (row >= max_menu_rows - MIN_BLANK_ROWS)
                    break;
            }

            if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
			{
				if((BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top))%2 == 0)
					draw_icon_locked(gMenuIcon[MENU_BUTTON_L_LOWHALF], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT - MENU_INCREMENT/2);
				else
					draw_icon_locked(gMenuIcon[MENU_BUTTON_R_LOWHALF], resX/2, menu_top*CHAR_HEIGHT + (i - menu_show_start - menu_top + 1)*MENU_INCREMENT - MENU_INCREMENT/2);
			}
			if (menu_show_start > 0)
			{
				draw_icon_locked(gMenuIcon[MENU_BUTTON_R_HALF], resX/2, menu_top*CHAR_HEIGHT + MENU_INCREMENT * 0.5);
			}       
        }
        rowOffset = menu_top*CHAR_HEIGHT + (row - menu_top + 2)*MENU_INCREMENT;
		if(row == 0)
			rowOffset=0;

        gr_color(NORMAL_TEXT_COLOR);
        int cur_row = text_row;
        int available_rows = total_rows - (rowOffset/CHAR_HEIGHT) - 1;
        int start_row = (rowOffset/CHAR_HEIGHT) + 1;
        if (available_rows < MAX_ROWS)
            cur_row = (cur_row + (MAX_ROWS - available_rows)) % MAX_ROWS;
        else
            start_row = total_rows - MAX_ROWS;

        int r;
        for (r = 0; r < (available_rows < MAX_ROWS ? available_rows : MAX_ROWS); r++) {
            draw_text_line(start_row + r, text[(cur_row + r) % MAX_ROWS], 0, !isMenu, 0);
        }
    }
    
    if (show_menu)
        draw_virtualkeys_locked();
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with gUpdateMutex locked.
static void update_screen_locked(void) {
    if (!ui_has_initialized)
        return;

    draw_screen_locked();
    gr_flip();
}

// Updates only the progress bar, if possible, otherwise redraws the screen.
// Should only be called with gUpdateMutex locked.
static void update_progress_locked(void) {
    if (!ui_has_initialized)
        return;

    // set minimum delay between progress updates if we have a text overlay
    // exception: gProgressScopeDuration != 0: to keep zip installer refresh behavior
    struct timeval curtime;
    gettimeofday(&curtime, NULL);
    long delta_ms = delta_milliseconds(lastprogupd, curtime);
    if (show_text && gProgressScopeDuration == 0 && lastprogupd.tv_sec > 0
            && delta_ms < UI_MIN_PROG_DELTA_MS) {
        return;
    }

    if (show_text || !gPagesIdentical) {
        draw_screen_locked();    // Must redraw the whole screen
        gPagesIdentical = 1;
    } else {
        draw_progress_locked();  // Draw only the progress bar and overlays
    }
    gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
static void *progress_thread(void *cookie) {
    double interval = 1.0 / ui_parameters.update_fps;
    for (;;) {
        double start = now();
        pthread_mutex_lock(&gUpdateMutex);

        int redraw = 0;

        // update the progress bar animation, if active
        // update the spinning cube animation, even if no progress bar
        if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE ||
                gCurrentIcon == BACKGROUND_ICON_INSTALLING) {
            redraw = 1;
        }

        // move the progress bar forward on timed intervals, if configured
        int duration = gProgressScopeDuration;
        if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL) {
            if (duration > 0) {
                double elapsed = now() - gProgressScopeTime;
                float progress = 1.0 * elapsed / duration;
                if (progress > 1.0) progress = 1.0;
                if (progress > gProgress) {
                    gProgress = progress;
                    redraw = 1;
                }
            }
        }

        if (redraw) update_progress_locked();

        pthread_mutex_unlock(&gUpdateMutex);
        double end = now();
        // minimum of 20ms delay between frames
        double delay = interval - (end-start);
        if (delay < 0.02) delay = 0.02;
        usleep((long)(delay * 1000000));
    }
    return NULL;
}

// handle the action associated with user input touch events inside the ui handler
int device_handle_mouse(struct keyStruct *key, int visible)
{
	int j=0;
	if(show_menu && visible)
	{
		if((key->code == KEY_SCROLLUP || key->code == KEY_SCROLLDOWN) && (abs(abs(key->length) - abs(key->Xlength)) < 0.2*resY ))
		{
				if(key->code == KEY_SCROLLDOWN)
				{
					selMenuButtonIcon = -1;
					if (menu_show_start > 0)
					{
						menu_show_start = menu_show_start-BUTTON_MAX_ROWS+BUTTON_EQUIVALENT(menu_top);
						if (menu_show_start < 0)
							menu_show_start = 0;
							selMenuButtonIcon = 0;
						return menu_show_start;
					}

					return GO_BACK;
				}
				else if	(key->code == KEY_SCROLLUP)
				{
					if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
					{
						menu_show_start = menu_show_start+BUTTON_MAX_ROWS-BUTTON_EQUIVALENT(menu_top) -2;
						selMenuButtonIcon = 1;
						return menu_show_start+1;
					}
				}
		}
		else if ((key->y < (resY - MENU_MAX_HEIGHT)) &&  (key->length < 0.1*resY))
		{
				if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
					j = BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top);
				else
					j = menu_items - menu_show_start;

				int rowOffset = menu_top*CHAR_HEIGHT;

				int sel_menu;
				if(key->x < resX/2 && key->y >= rowOffset)
				{
					sel_menu = (int)((key->y - rowOffset)/MENU_HEIGHT );
					sel_menu = sel_menu*2;
				}
				else if(key->x >= resX/2 && key->y >= (rowOffset + MENU_INCREMENT))
				{
					sel_menu = (int)((key->y - rowOffset - MENU_INCREMENT)/MENU_HEIGHT );
					sel_menu = sel_menu*2 + 1;
				}
				else
					return -1;

				if(key->y > rowOffset && key->y < rowOffset + (j+1)*MENU_INCREMENT)
				{
					selMenuButtonIcon = -1;
					if(sel_menu+menu_show_start < 0)
						return 0;
					if (sel_menu == j)
						return -1;

					return sel_menu+menu_show_start;
			}
		}
		else if((key->y > (resY - MENU_MAX_HEIGHT))  &&  (key->length < 0.1*resY))
		{
					//ToDo: Following structure should be global
				struct { int x; int y; int xL; int xR; } MENU_ICON[] = {
					{  get_menu_icon_info(MENU_BACK,MENU_ICON_X),	get_menu_icon_info(MENU_BACK,MENU_ICON_Y), get_menu_icon_info(MENU_BACK,MENU_ICON_XL), get_menu_icon_info(MENU_BACK,MENU_ICON_XR) },
					{  get_menu_icon_info(MENU_DOWN,MENU_ICON_X),	get_menu_icon_info(MENU_DOWN,MENU_ICON_Y), get_menu_icon_info(MENU_DOWN,MENU_ICON_XL), get_menu_icon_info(MENU_DOWN,MENU_ICON_XR) },
					{  get_menu_icon_info(MENU_UP,MENU_ICON_X),	get_menu_icon_info(MENU_UP,MENU_ICON_Y), get_menu_icon_info(MENU_UP,MENU_ICON_XL), get_menu_icon_info(MENU_UP,MENU_ICON_XR) },
					{  get_menu_icon_info(MENU_SELECT,MENU_ICON_X),	get_menu_icon_info(MENU_SELECT,MENU_ICON_Y), get_menu_icon_info(MENU_SELECT,MENU_ICON_XL), get_menu_icon_info(MENU_SELECT,MENU_ICON_XR) },
				};
				int position;
				position = key->x;
				if(position > MENU_ICON[MENU_BACK].xL && position < MENU_ICON[MENU_BACK].xR)
					return GO_BACK;
				else if(position > MENU_ICON[MENU_DOWN].xL && position < MENU_ICON[MENU_DOWN].xR)
					return HIGHLIGHT_DOWN;
				else if(position > MENU_ICON[MENU_UP].xL && position < MENU_ICON[MENU_UP].xR)
					return HIGHLIGHT_UP;
				else if(position > MENU_ICON[MENU_SELECT].xL && position < MENU_ICON[MENU_SELECT].xR)
					return SELECT_ITEM;
		}
	}
	return NO_ACTION;
}

// handle the user input events (mainly the touch events) inside the ui handler
static void ui_handle_mouse_input(int* curPos)
{
	pthread_mutex_lock(&key_queue_mutex);

//ToDo: Following structure should be global
	struct { int x; int y; int xL; int xR; } MENU_ICON[] = {
		{  get_menu_icon_info(MENU_BACK,MENU_ICON_X),	get_menu_icon_info(MENU_BACK,MENU_ICON_Y), get_menu_icon_info(MENU_BACK,MENU_ICON_XL), get_menu_icon_info(MENU_BACK,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_DOWN,MENU_ICON_X),	get_menu_icon_info(MENU_DOWN,MENU_ICON_Y), get_menu_icon_info(MENU_DOWN,MENU_ICON_XL), get_menu_icon_info(MENU_DOWN,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_UP,MENU_ICON_X),	get_menu_icon_info(MENU_UP,MENU_ICON_Y), get_menu_icon_info(MENU_UP,MENU_ICON_XL), get_menu_icon_info(MENU_UP,MENU_ICON_XR) },
		{  get_menu_icon_info(MENU_SELECT,MENU_ICON_X),	get_menu_icon_info(MENU_SELECT,MENU_ICON_Y), get_menu_icon_info(MENU_SELECT,MENU_ICON_XL), get_menu_icon_info(MENU_SELECT,MENU_ICON_XR) },
	};

  if (show_menu) {
    if (curPos[0] > 0) {
		int positionX,positionY;

		positionX = curPos[1];
		positionY = curPos[2];

		pthread_mutex_lock(&gUpdateMutex);
		struct stat info;
		if(positionY < (resY - MENU_MAX_HEIGHT)) {
				int j=0;

				if (menu_items - menu_show_start + BUTTON_EQUIVALENT(menu_top) > BUTTON_MAX_ROWS)
					j = BUTTON_MAX_ROWS - BUTTON_EQUIVALENT(menu_top);
				else
					j = menu_items - menu_show_start;

				int rowOffset = menu_top*CHAR_HEIGHT;
				int sel_menu;
				if(positionX < resX/2 && positionY >= rowOffset)
				{
					sel_menu = (int)((positionY - rowOffset)/MENU_HEIGHT );
					sel_menu = sel_menu*2;
				}
				else if(positionX >= resX/2 && positionY >= (rowOffset + MENU_INCREMENT))
				{
					sel_menu = (int)((positionY - rowOffset - MENU_INCREMENT)/MENU_HEIGHT );
					sel_menu = sel_menu*2 + 1;
				}
				else
					sel_menu = -1;

				if(selMenuButtonIcon < 0)
					selMenuButtonIcon = 0;

				if(positionY > rowOffset && positionY < rowOffset + (j+1)*MENU_INCREMENT && selMenuButtonIcon != sel_menu && sel_menu != j && sel_menu >= 0)
				{
					if (sel_menu %2 == 0)
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_L_SEL], resX/2, menu_top*CHAR_HEIGHT + (sel_menu+1)*MENU_INCREMENT);
			            gr_color(255, 255, 255, 255);
						if(menu[sel_menu + menu_show_start + menu_top][0] != '-')
			                draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_LEFT_OFFSET);
			            else
			            {
			                draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2+1, 1, MENU_ITEM_LEFT_OFFSET);
			                draw_text_line(sel_menu , submenu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2+1, 1, MENU_ITEM_LEFT_OFFSET);
			            }
					}
					else
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_R_SEL], resX/2, menu_top*CHAR_HEIGHT + (sel_menu+1)*MENU_INCREMENT);
			            gr_color(255, 255, 255, 255);
						if(menu[sel_menu + menu_show_start + menu_top][0] != '-')
			                draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_RIGHT_OFFSET);
			            else
			            {
			                draw_text_line(sel_menu , menu[sel_menu + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2+1, 1, MENU_ITEM_RIGHT_OFFSET);
			                draw_text_line(sel_menu , submenu[sel_menu + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2+1, 1, MENU_ITEM_RIGHT_OFFSET);
			            }
					}
					if (selMenuButtonIcon %2 == 0)
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_L], resX/2, menu_top*CHAR_HEIGHT + (selMenuButtonIcon+1)*MENU_INCREMENT);
			            gr_color(menuTextColor[0], menuTextColor[1], menuTextColor[2], menuTextColor[3]);
						if(menu[selMenuButtonIcon + menu_show_start + menu_top][0] != '-')
			                draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_LEFT_OFFSET);
			            else
			            {
			                draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2+1, 1, MENU_ITEM_LEFT_OFFSET);
			                draw_text_line(selMenuButtonIcon , submenu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2+1, 1, MENU_ITEM_LEFT_OFFSET);
			            }

					}
					else
					{
						draw_icon_locked(gMenuIcon[MENU_BUTTON_R], resX/2, menu_top*CHAR_HEIGHT + (selMenuButtonIcon+1)*MENU_INCREMENT);
			            gr_color(menuTextColor[0], menuTextColor[1], menuTextColor[2], menuTextColor[3]);
						if(menu[selMenuButtonIcon + menu_show_start + menu_top][0] != '-')
			                draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER, 1, MENU_ITEM_RIGHT_OFFSET);
			            else
			            {
			                draw_text_line(selMenuButtonIcon , menu[selMenuButtonIcon + menu_show_start + menu_top]+1, rowOffset-MENU_CENTER-CHAR_HEIGHT/2+1, 1, MENU_ITEM_RIGHT_OFFSET);
			                draw_text_line(selMenuButtonIcon , submenu[selMenuButtonIcon + menu_show_start + menu_top], rowOffset-MENU_CENTER+CHAR_HEIGHT/2+1, 1, MENU_ITEM_RIGHT_OFFSET);
			            }

					}
					selMenuButtonIcon = sel_menu;
					menu_sel = sel_menu;
					gr_flip();
				}
		}
		else {
				if(positionX > MENU_ICON[MENU_BACK].xL && positionX < MENU_ICON[MENU_BACK].xR) {
					draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y);
					draw_icon_locked(gMenuIcon[MENU_BACK_M], MENU_ICON[MENU_BACK].x, MENU_ICON[MENU_BACK].y);
					selMenuIcon = MENU_BACK;
					gr_flip();
				}
				else if(positionX > MENU_ICON[MENU_DOWN].xL && positionX < MENU_ICON[MENU_DOWN].xR) {
					draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y);
					draw_icon_locked(gMenuIcon[MENU_DOWN_M], MENU_ICON[MENU_DOWN].x, MENU_ICON[MENU_DOWN].y);
					selMenuIcon = MENU_DOWN;
					gr_flip();
				}
				else if(positionX > MENU_ICON[MENU_UP].xL && positionX < MENU_ICON[MENU_UP].xR) {
					draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y);
					draw_icon_locked(gMenuIcon[MENU_UP_M], MENU_ICON[MENU_UP].x, MENU_ICON[MENU_UP].y);
					selMenuIcon = MENU_UP;
					gr_flip();
				}
				else if(positionX > MENU_ICON[MENU_SELECT].xL && positionX < MENU_ICON[MENU_SELECT].xR) {
					draw_icon_locked(gMenuIcon[selMenuIcon], MENU_ICON[selMenuIcon].x, MENU_ICON[selMenuIcon].y);
					draw_icon_locked(gMenuIcon[MENU_SELECT_M], MENU_ICON[MENU_SELECT].x, MENU_ICON[MENU_SELECT].y);
					selMenuIcon = MENU_SELECT;
					gr_flip();
				}
		}
		key_queue_len_back = key_queue_len;
		pthread_mutex_unlock(&gUpdateMutex);
     }
  }
  pthread_mutex_unlock(&key_queue_mutex);
}

static int rel_sum = 0;
static int rel_sum_x = 0;
static int rel_sum_y = 0;

static int input_callback(int fd, short revents, void *data) {
    struct input_event ev;
    int ret;
    int fake_key = 0;
    int got_data = 0;

    ret = ev_get_input(fd, revents, &ev);
    if (ret)
        return -1;

    if (ev.type == EV_SYN) {
        // end of a multitouch point
        if (ev.code == SYN_MT_REPORT) {
			if (actPos.x != 0)
				backupPos.x = actPos.x;
			else
				actPos.x = backupPos.x;
				
			if (actPos.y != 0)
				backupPos.y = actPos.y;
			else
				actPos.y = backupPos.y;
			if (touchY > 0 && actPos.y < touchY) {
				actPos.num = 0;
				actPos.x = 0;
				actPos.y = 0;
				actPos.pressure = 0;
				actPos.size = 0;
			}
	
			if (actPos.num>=0 && actPos.num<MAX_MT_POINTS) {
				// create a fake keyboard event. We will use BTN_WHEEL, BTN_GEAR_DOWN and BTN_GEAR_UP key events to fake
				// TOUCH_MOVE, TOUCH_DOWN and TOUCH_UP in this order
				int type = BTN_WHEEL;
				// new and old pressure state are not consistent --> we have touch down or up event
				if ((mousePos[actPos.num].pressure!=0) != (actPos.pressure!=0)) {
					if (actPos.pressure == 0) {
						type = BTN_GEAR_UP;
						if (actPos.num==0) {
							if (mousePos[0].length<15) {
								// consider this a mouse click
								type = BTN_MOUSE;
							}
							memset(&grabPos,0,sizeof(grabPos));
						}
					} else if (actPos.pressure != 0) {
						type == BTN_GEAR_DOWN;
						if (actPos.num==0) {
							grabPos = actPos;
						}
					}
				}
				
				fake_key = 1;
				ev.type = EV_KEY;
				ev.code = type;
				ev.value = actPos.num+1;
				
				// this should be locked, but that causes ui events to get dropped, as the screen drawing takes too much time
				// this should be solved by making the critical section inside the drawing much much smaller
				if (actPos.pressure) {
                  if (mousePos[actPos.num].pressure) {
                    actPos.length = mousePos[actPos.num].length + abs(mousePos[actPos.num].x-actPos.x) + abs(mousePos[actPos.num].y-actPos.y);
					if ( ((mousePos[actPos.num].x-actPos.x) < 0 && mousePos[actPos.num].Xlength > 0) || ((mousePos[actPos.num].x-actPos.x) > 0 && mousePos[actPos.num].Xlength < 0))
					{
						actPos.Xlength = mousePos[actPos.num].x-actPos.x;
					}
					else
					{
						actPos.Xlength = mousePos[actPos.num].Xlength + mousePos[actPos.num].x-actPos.x;
					}
                  } else {
                    actPos.length = 0;
                    actPos.Xlength = 0;
                  }
                } else {
					if (abs(mousePos[actPos.num].Xlength) > (0.1*resX))
					{
						if (mousePos[actPos.num].Xlength > 0)
						{
							ev.code = KEY_SCROLLDOWN;
						}
						else
						{
							ev.code = KEY_SCROLLUP;
						}
					}
                  actPos.length = 0;
                  actPos.Xlength = 0;
				}
				oldMousePos[actPos.num] = mousePos[actPos.num];
				mousePos[actPos.num] = actPos;
				int curPos[] = {actPos.pressure, actPos.x, actPos.y};
				ui_handle_mouse_input(curPos);
			}
			
			memset(&actPos,0,sizeof(actPos));
		} else {
			return 0;
		}
	} else if (ev.type == EV_ABS) {
		  // multitouch records are sent as ABS events. Well at least on the SGS-i9000
		  if (ev.code == ABS_MT_POSITION_X) {
		    actPos.x = MT_X(fd, ev.value);
		  } else if (ev.code == ABS_MT_POSITION_Y) {
		    actPos.y = MT_Y(fd, ev.value);
 		  } else if (ev.code == ABS_MT_TOUCH_MAJOR) {
 		    actPos.pressure = ev.value; 
		  } else if (ev.code == ABS_MT_PRESSURE) {
		    actPos.pressure = ev.value; // on normal devices
		  } else if (ev.code == ABS_MT_WIDTH_MAJOR) {
		    // num is stored inside the high byte of width. Well at least on SGS-i9000
		    if (actPos.num==0) {
		      // only update if it was not already set. On a normal device MT_TRACKING_ID is sent
		      actPos.num = ev.value >> 8;
		    }
		    actPos.size = ev.value & 0xFF;
		  } else if (ev.code == ABS_MT_TRACKING_ID) {
		    // on a normal device, the num is got from this value
		    actPos.num = ev.value;
		  }
    } else if (ev.type == EV_REL) {
        if (ev.code == REL_Y) {
			// accumulate the up or down motion reported by
			// the trackball.  When it exceeds a threshol
			// (positive or negative), fake an up/down
			// key event.
			rel_sum_y += ev.value;
			if (rel_sum_y > 3) { 
				fake_key = 1;
				ev.type = EV_KEY;
				ev.code = KEY_DOWN;
				ev.value = 1;
				rel_sum_y = 0;
            } else if (rel_sum_y < -3) {
				fake_key = 1;
				ev.type = EV_KEY;
				ev.code = KEY_UP;
				ev.value = 1;
				rel_sum_y = 0;
            }
		}
		// do the same for the X axis
		if (ev.code == REL_X) {
			rel_sum_x += ev.value;
			if (rel_sum_x > 3) {
				fake_key = 1;
				ev.type = EV_KEY;
				ev.code = KEY_RIGHT;
				ev.value = 1;
				rel_sum_x = 0;
            } else if (rel_sum_x < -3) {
				fake_key = 1;
				ev.type = EV_KEY;
				ev.code = KEY_LEFT;
				ev.value = 1;
				rel_sum_x = 0;
            }
        }
    } else {
        rel_sum = 0;
        rel_sum_y = 0;
        rel_sum_x = 0;
    }

    if (ev.type != EV_KEY || ev.code > KEY_MAX)
        return 0;

    if (ev.value == 2) {
        boardEnableKeyRepeat = 0;
    }

    pthread_mutex_lock(&key_queue_mutex);
    if (!fake_key) {
        // our "fake" keys only report a key-down event (no
        // key-up), so don't record them in the key_pressed
        // table.
        key_pressed[ev.code] = ev.value;
    }
    const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
    if (ev.value > 0 && key_queue_len < queue_max) {
        if (ev.code!=BTN_WHEEL || key_queue_len==0 || key_queue[key_queue_len-1]!=BTN_WHEEL) {
			key_queue[key_queue_len++] = ev.code;
		}

        if (boardEnableKeyRepeat) {
            struct timeval now;
            gettimeofday(&now, NULL);

            key_press_time[ev.code] = (now.tv_sec * 1000) + (now.tv_usec / 1000);
            key_last_repeat[ev.code] = 0;
        }

        pthread_cond_signal(&key_queue_cond);
    }
    pthread_mutex_unlock(&key_queue_mutex);

    if (ev.value > 0 && device_toggle_display(key_pressed, ev.code)) {
        pthread_mutex_lock(&gUpdateMutex);
        show_text = !show_text;
        if (show_text) show_text_ever = 1;
        update_screen_locked();
        pthread_mutex_unlock(&gUpdateMutex);
    }

    if (ev.value > 0 && device_reboot_now(key_pressed, ev.code)) {
        reboot_main_system(ANDROID_RB_RESTART, 0, 0);
    }

    return 0;
}

// Reads input events, handles special hot keys, and adds to the key queue.
static void *input_thread(void *cookie) {
    for (;;) {
        if (!ev_wait(-1))
            ev_dispatch();
    }
    return NULL;
}

void ui_init(void) {
    ui_has_initialized = 1;
    gr_init();
    ev_init(input_callback, NULL);

    text_col = text_row = 0;
    text_rows = gr_fb_height() / CHAR_HEIGHT;
    max_menu_rows = text_rows - MIN_LOG_ROWS;
    if (max_menu_rows > MENU_MAX_ROWS)
        max_menu_rows = MENU_MAX_ROWS;
    if (text_rows > MAX_ROWS) text_rows = MAX_ROWS;
    text_top = 1;

    text_cols = gr_fb_width() / CHAR_WIDTH;
    if (text_cols > MAX_COLS - 1) text_cols = MAX_COLS - 1;

    int i;
    for (i = 0; BITMAPS[i].name != NULL; ++i) {
        int result = res_create_surface(BITMAPS[i].name, BITMAPS[i].surface);
        if (result < 0) {
            LOGE("Missing bitmap %s\n(Code %d)\n", BITMAPS[i].name, result);
        }
    }

    gProgressBarIndeterminate = malloc(ui_parameters.indeterminate_frames *
                                       sizeof(gr_surface));
    for (i = 0; i < ui_parameters.indeterminate_frames; ++i) {
        char filename[40];
        // "indeterminate01.png", "indeterminate02.png", ...
        sprintf(filename, "indeterminate%02d", i+1);
        int result = res_create_surface(filename, gProgressBarIndeterminate+i);
        if (result < 0) {
            LOGE("Missing bitmap %s\n(Code %d)\n", filename, result);
        }
    }

    if (ui_parameters.installing_frames > 0) {
        gInstallationOverlay = malloc(ui_parameters.installing_frames *
                                      sizeof(gr_surface));
        for (i = 0; i < ui_parameters.installing_frames; ++i) {
            char filename[40];
            // "icon_installing_overlay01.png",
            // "icon_installing_overlay02.png", ...
            sprintf(filename, "icon_installing_overlay%02d", i+1);
            int result = res_create_surface(filename, gInstallationOverlay+i);
            if (result < 0) {
                LOGE("Missing bitmap %s\n(Code %d)\n", filename, result);
            }
        }

        // Adjust the offset to account for the positioning of the
        // base image on the screen.
        if (gBackgroundIcon[BACKGROUND_ICON_INSTALLING] != NULL) {
            gr_surface bg = gBackgroundIcon[BACKGROUND_ICON_INSTALLING];
            ui_parameters.install_overlay_offset_x +=
                (gr_fb_width() - gr_get_width(bg)) / 2;
            ui_parameters.install_overlay_offset_y +=
                (gr_fb_height() - gr_get_height(bg)) / 2;
        }
    } else {
        gInstallationOverlay = NULL;
    }

    char enable_key_repeat[PROPERTY_VALUE_MAX];
    property_get("ro.cwm.enable_key_repeat", enable_key_repeat, "");
    if (!strcmp(enable_key_repeat, "true") || !strcmp(enable_key_repeat, "1")) {
        boardEnableKeyRepeat = 1;

        char key_list[PROPERTY_VALUE_MAX];
        property_get("ro.cwm.repeatable_keys", key_list, "");
        if (strlen(key_list) == 0) {
            boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_UP;
            boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_DOWN;
            boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_VOLUMEUP;
            boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_VOLUMEDOWN;
        } else {
            char *pch = strtok(key_list, ",");
            while (pch != NULL) {
                boardRepeatableKeys[boardNumRepeatableKeys++] = atoi(pch);
                pch = strtok(NULL, ",");
            }
        }
    }

    memset(&actPos, 0, sizeof(actPos));
    memset(&grabPos, 0, sizeof(grabPos));
    memset(mousePos, 0, sizeof(mousePos));
    memset(oldMousePos, 0, sizeof(oldMousePos));

    pt_ui_thread_active = 1;
    pt_input_thread_active = 1;

    pthread_create(&pt_ui_thread, NULL, progress_thread, NULL);
    pthread_create(&pt_input_thread, NULL, input_thread, NULL);
}

char *ui_copy_image(int icon, int *width, int *height, int *bpp) {
    pthread_mutex_lock(&gUpdateMutex);
    draw_background_locked(icon);
    *width = gr_fb_width();
    *height = gr_fb_height();
    *bpp = sizeof(gr_pixel) * 8;
    int size = *width * *height * sizeof(gr_pixel);
    char *ret = malloc(size);
    if (ret == NULL) {
        LOGE("Can't allocate %d bytes for image\n", size);
    } else {
        memcpy(ret, gr_fb_data(), size);
    }
    pthread_mutex_unlock(&gUpdateMutex);
    return ret;
}

void ui_set_background(int icon) {
    pthread_mutex_lock(&gUpdateMutex);
    gCurrentIcon = icon;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_indeterminate_progress() {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    if (gProgressBarType != PROGRESSBAR_TYPE_INDETERMINATE) {
        gProgressBarType = PROGRESSBAR_TYPE_INDETERMINATE;
        update_progress_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_progress(float portion, int seconds) {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NORMAL;
    gProgressScopeStart += gProgressScopeSize;
    gProgressScopeSize = portion;
    gProgressScopeTime = now();
    gProgressScopeDuration = seconds;
    gProgress = 0;
    update_progress_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_set_progress(float fraction) {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL && fraction > gProgress) {
        // Skip updates that aren't visibly different.
        int width = gr_get_width(gProgressBarIndeterminate[0]);
        float scale = width * gProgressScopeSize;
        if ((int) (gProgress * scale) != (int) (fraction * scale)) {
            gProgress = fraction;
            update_progress_locked();
        }
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_reset_progress() {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NONE;
    gProgressScopeStart = 0;
    gProgressScopeSize = 0;
    gProgressScopeTime = 0;
    gProgressScopeDuration = 0;
    gProgress = 0;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

int ui_get_text_cols() {
    return text_cols;
}

void ui_print(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);

    if (ui_log_stdout)
        fputs(buf, stdout);

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_printlogtail(int nb_lines) {
    char * log_data;
    char tmp[PATH_MAX];
    FILE * f;
    int line=0;
    //don't log output to recovery.log
    ui_log_stdout=0;
    sprintf(tmp, "tail -n %d /tmp/recovery.log > /tmp/tail.log", nb_lines);
    __system(tmp);
    f = fopen("/tmp/tail.log", "rb");
    if (f != NULL) {
        while (line < nb_lines) {
            log_data = fgets(tmp, PATH_MAX, f);
            if (log_data == NULL) break;
            ui_print("%s", tmp);
            line++;
        }
        fclose(f);
    }
    ui_print("Return to menu with any key.\n");
    ui_log_stdout=1;
}

#define ALLOWED_CHAR (int)(resX*0.47)/CHAR_WIDTH
int ui_start_menu(const char** headers, char** items, int initial_selection) {
    int i,j;
	int remChar;
	selMenuButtonIcon=0;
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        for (i = 0; i < text_rows; ++i) {
            if (headers[i] == NULL) break;
			remChar = (int)(resX - strlen(headers[i])*CHAR_WIDTH)/(CHAR_WIDTH*2);  
			for (j = 0; j < remChar; j++) {
				strcpy(menu[i]+j, " ");
			}
            strncpy(menu[i]+remChar, headers[i], text_cols- remChar);
            menu[i][text_cols-remChar] = '\0';
        }
        menu_top = i;
        for (; i < MENU_MAX_ROWS; ++i) {
            if (items[i-menu_top] == NULL) break;
            if (strlen(items[i-menu_top]) > ALLOWED_CHAR )	
			{
			    strcpy(menu[i], MENU_ITEM_HEADER);
			    strncpy(menu[i] + MENU_ITEM_HEADER_LENGTH, items[i-menu_top], ALLOWED_CHAR - MENU_ITEM_HEADER_LENGTH);
			    if(strlen(items[i-menu_top]) > (2*ALLOWED_CHAR - 1) )
			    {
				strncpy(submenu[i], items[i-menu_top] + ALLOWED_CHAR - MENU_ITEM_HEADER_LENGTH, ALLOWED_CHAR-3);
				strcpy(submenu[i] + ALLOWED_CHAR-3, "..." );
			    }
			    else
				strncpy(submenu[i], items[i-menu_top] + ALLOWED_CHAR - MENU_ITEM_HEADER_LENGTH, MENU_MAX_COLS-1 - MENU_ITEM_HEADER_LENGTH);
			}
            else
			{
				strncpy(menu[i], items[i-menu_top], MENU_MAX_COLS-1);
			}
	            menu[i][MENU_MAX_COLS-1] = '\0';
        }

		gShowBackButton = 0;
        if (gShowBackButton && !ui_root_menu) {
            strcpy(menu[i], " - <<-  Go Back   ");
            ++i;
        }

        menu_items = i - menu_top;
        show_menu = 1;
        menu_sel = menu_show_start = initial_selection;
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
    if (gShowBackButton && !ui_root_menu) {
        return menu_items - 1;
    }
    return menu_items;
}

int ui_menu_select(int sel) {
    int old_sel;
    pthread_mutex_lock(&gUpdateMutex);
    if (show_menu > 0) {
        old_sel = menu_sel;
        menu_sel = sel;

        if (menu_sel < 0) menu_sel = menu_items + menu_sel;
        if (menu_sel >= menu_items) menu_sel = menu_sel - menu_items;


        if (menu_sel < menu_show_start && menu_show_start > 0) {
            menu_show_start = menu_sel;
        }

        if (menu_sel - menu_show_start + menu_top >= BUTTON_MAX_ROWS) {
            menu_show_start = menu_sel + menu_top - BUTTON_MAX_ROWS + 1;
        }

        sel = menu_sel;

        if (menu_sel != old_sel) update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
    return sel;
}

void ui_end_menu() {
    int i;
    pthread_mutex_lock(&gUpdateMutex);
    if (show_menu > 0 && text_rows > 0 && text_cols > 0) {
        show_menu = 0;
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

int ui_text_visible() {
    pthread_mutex_lock(&gUpdateMutex);
    int visible = show_text;
    pthread_mutex_unlock(&gUpdateMutex);
    return visible;
}

int ui_text_ever_visible() {
    pthread_mutex_lock(&gUpdateMutex);
    int ever_visible = show_text_ever;
    pthread_mutex_unlock(&gUpdateMutex);
    return ever_visible;
}

void ui_show_text(int visible) {
    pthread_mutex_lock(&gUpdateMutex);
    show_text = visible;
    if (show_text) show_text_ever = 1;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

static int usb_connected() {
    int fd = open("/sys/class/android_usb/android0/state", O_RDONLY);
    if (fd < 0) {
        printf("failed to open /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
        return 0;
    }

    char buf;
    /* USB is connected if android_usb state is CONNECTED or CONFIGURED */
    int connected = (read(fd, &buf, 1) == 1) && (buf == 'C');
    if (close(fd) < 0) {
        printf("failed to close /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
    }
    return connected;
}

void ui_cancel_wait_key() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue[key_queue_len] = -2;
    key_queue_len++;
    pthread_cond_signal(&key_queue_cond);
    pthread_mutex_unlock(&key_queue_mutex);
}

struct keyStruct *ui_wait_key() {
    if (boardEnableKeyRepeat){
		key.code = ui_wait_key_with_repeat();
		return &key;
    }
    pthread_mutex_lock(&key_queue_mutex);
    key.code = -1;
    int timeouts = UI_WAIT_KEY_TIMEOUT_SEC;

    // Time out after REFRESH_TIME_USB_INTERVAL seconds to catch volume changes, and loop for
    // UI_WAIT_KEY_TIMEOUT_SEC to restart a device not connected to USB
    do {
        struct timeval now;
        struct timespec timeout;
        gettimeofday(&now, NULL);
        timeout.tv_sec = now.tv_sec;
        timeout.tv_nsec = now.tv_usec * 1000;
        timeout.tv_sec += REFRESH_TIME_USB_INTERVAL;

        int rc = 0;
        while (key_queue_len == 0 && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&key_queue_cond, &key_queue_mutex,
                                        &timeout);
            if (volumes_changed()) {
                pthread_mutex_unlock(&key_queue_mutex);
                return REFRESH;
            }
        }
        timeouts -= REFRESH_TIME_USB_INTERVAL;
    } while ((timeouts > 0 || usb_connected()) && key_queue_len == 0);

    if (key_queue_len > 0) {
        key.code = key_queue[0];
        memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
    }
    
    if((key.code == BTN_GEAR_UP || key.code == BTN_MOUSE) && !actPos.pressure && oldMousePos[actPos.num].pressure && key_queue_len_back != (key_queue_len -1))
	{
		key.code = ABS_MT_POSITION_X;
		key.x = oldMousePos[actPos.num].x;
		key.y = oldMousePos[actPos.num].y;
	}
 
	key.length = oldMousePos[actPos.num].length;
	key.Xlength = oldMousePos[actPos.num].Xlength;
    pthread_mutex_unlock(&key_queue_mutex);
    return &key;
}

static int key_can_repeat(int key) {
    int k = 0;
    for (;k < boardNumRepeatableKeys; ++k) {
        if (boardRepeatableKeys[k] == key) {
            break;
        }
    }
    if (k < boardNumRepeatableKeys) return 1;
    return 0;
}

static int ui_wait_key_with_repeat() {
    int keyVal = -1;

    // Loop to wait for more keys
    do {
        int timeouts = UI_WAIT_KEY_TIMEOUT_SEC;
        int rc = 0;
        struct timeval now;
        struct timespec timeout;
        pthread_mutex_lock(&key_queue_mutex);
        while (key_queue_len == 0 && timeouts > 0) {
            gettimeofday(&now, NULL);
            timeout.tv_sec = now.tv_sec;
            timeout.tv_nsec = now.tv_usec * 1000;
            timeout.tv_sec += REFRESH_TIME_USB_INTERVAL;

            rc = 0;
            while (key_queue_len == 0 && rc != ETIMEDOUT) {
                rc = pthread_cond_timedwait(&key_queue_cond, &key_queue_mutex,
                                            &timeout);
                if (volumes_changed()) {
                    pthread_mutex_unlock(&key_queue_mutex);
                    return REFRESH;
                }
            }
            timeouts -= REFRESH_TIME_USB_INTERVAL;
        }
        pthread_mutex_unlock(&key_queue_mutex);

        if (rc == ETIMEDOUT && !usb_connected()) {
            keyVal = -1;
            return &key;
        }

        // Loop to wait wait for more keys, or repeated keys to be ready.
        while (1) {
            unsigned long now_msec;

            gettimeofday(&now, NULL);
            now_msec = (now.tv_sec * 1000) + (now.tv_usec / 1000);

            pthread_mutex_lock(&key_queue_mutex);

            // Replacement for the while conditional, so we don't have to lock the entire
            // loop, because that prevents the input system from touching the variables while
            // the loop is running which causes problems.
            if (key_queue_len == 0) {
                pthread_mutex_unlock(&key_queue_mutex);
                break;
            }

            keyVal = key_queue[0];
            memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);

            // sanity check the returned key.
            if (keyVal < 0) {
                pthread_mutex_unlock(&key_queue_mutex);
                return &key;
            }

            // Check for already released keys and drop them if they've repeated.
            if (!key_pressed[keyVal] && key_last_repeat[keyVal] > 0) {
                pthread_mutex_unlock(&key_queue_mutex);
                continue;
            }

            if (key_can_repeat(keyVal)) {
                // Re-add the key if a repeat is expected, since we just popped it. The
                // if below will determine when the key is actually repeated (returned)
                // in the mean time, the key will be passed through the queue over and
                // over and re-evaluated each time.
                if (key_pressed[keyVal]) {
                    key_queue[key_queue_len] = keyVal;
                    key_queue_len++;
                }
                if ((now_msec > key_press_time[keyVal] + UI_KEY_WAIT_REPEAT && now_msec > key_last_repeat[keyVal] + UI_KEY_REPEAT_INTERVAL) ||
                        key_last_repeat[keyVal] == 0) {
                    key_last_repeat[keyVal] = now_msec;
                } else {
                    // Not ready
                    pthread_mutex_unlock(&key_queue_mutex);
                    continue;
                }
            }
            pthread_mutex_unlock(&key_queue_mutex);
            return &key;
        }
    } while (1);

    return &key;
}

int ui_key_pressed(struct keyStruct *key) {
    // This is a volatile static array, don't bother locking
    return key_pressed[key->code];
}

void ui_clear_key_queue() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue_len = 0;
    pthread_mutex_unlock(&key_queue_mutex);
}

void ui_set_log_stdout(int enabled) {
    ui_log_stdout = enabled;
}

int ui_should_log_stdout() {
    return ui_log_stdout;
}

void ui_set_show_text(int value) {
    show_text = value;
}

void ui_set_showing_back_button(int showBackButton) {
    gShowBackButton = showBackButton;
}

int ui_get_showing_back_button() {
    return 1;
}

int ui_is_showing_back_button() {
    return gShowBackButton && !ui_root_menu;
}

int ui_get_selected_item() {
  return menu_sel;
}

int ui_handle_key(int key, int visible) {
    return device_handle_key(key, visible);
}

void ui_delete_line() {
    pthread_mutex_lock(&gUpdateMutex);
    text[text_row][0] = '\0';
    text_row = (text_row - 1 + text_rows) % text_rows;
    text_col = 0;
    pthread_mutex_unlock(&gUpdateMutex);
}

static void ui_rainbow_mode() {
    static int colors[] = { 255, 0, 0,        // red
                            255, 127, 0,      // orange
                            255, 255, 0,      // yellow
                            0, 255, 0,        // green
                            60, 80, 255,      // blue
                            143, 0, 255 };    // violet

    gr_color(colors[cur_rainbow_color], colors[cur_rainbow_color+1], colors[cur_rainbow_color+2], 255);
    cur_rainbow_color += 3;
    if (cur_rainbow_color >= (sizeof(colors) / sizeof(colors[0]))) cur_rainbow_color = 0;
}

int ui_get_rainbow_mode() {
    return gRainbowMode;
}

void ui_set_rainbow_mode(int rainbowMode) {
    gRainbowMode = rainbowMode;

    pthread_mutex_lock(&gUpdateMutex);
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

int is_ui_initialized() {
    return ui_has_initialized;
}
