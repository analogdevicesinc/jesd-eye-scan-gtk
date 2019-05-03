/***************************************************************************//**
*   @file   jesd_status.c
*   @brief  JESD204 Status Information Utility
*   @author Michael Hennerich (michael.hennerich@analog.com)
********************************************************************************
* Copyright 2019 (c) Analog Devices, Inc.
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*  - Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  - Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in
*    the documentation and/or other materials provided with the
*    distribution.
*  - Neither the name of Analog Devices, Inc. nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*  - The use of this software may or may not infringe the patent rights
*    of one or more patent holders.  This license does not release you
*    from the requirement that you obtain separate licenses from these
*    patent holders to use this software.
*  - Use of the software either in source or binary form, must be run
*    on or directly connected to an Analog Devices Inc. component.
*
* THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "jesd_common.h"

#define MAX(a,b) (((a)>(b))?(a):(b))

#define JESD204_DEVICE_NAME 	"jesd204"
#define XCVR_DEVICE_NAME 	"adxcvr"

#define COL_SPACEING 3

enum color_pairs {
	C_NORM = 1,
	C_GOOD = 2,
	C_ERR = 3,
	C_CRIT = 4,
};

struct jesd204b_laneinfo lane_info[MAX_LANES];
char basedir[PATH_MAX];
char jesd_devices[MAX_DEVICES][PATH_MAX];
WINDOW *main_win, *stat_win, *dev_win, *lane_win;

static const char *link_status_lables[] = {
	"Link is",
	"Link Status",
	"Measured Link Clock",
	"Reported Link Clock",
	"Lane rate",
	"Lane rate / 40",
	"LMFC rate",
	"SYSREF captured",
	"SYSREF alignment error",
	"SYNC~",
	NULL
};

static const char *lane_status_lables[] = {
	"Lane#",
	"Errors",
	"Latency (Multiframes/Octets)",
	"CGS State",
	"Initial Frame Sync",
	"Initial Lane Alignment Sequence",
	NULL
};

void terminal_start()
{
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	curs_set(0);
}

void terminal_stop()
{
	endwin();
}

int jesd_get_strlen(WINDOW *win, int x)
{
	int x1, y1;

	getyx(win, y1, x1);

	return x1 - x;
}

int jesd_maxx(int x, int x1)
{
	return MAX(x, x1);
}

void jesd_clear_win(WINDOW *win, int simple)
{
	wclear(win);

	if (!simple)
		box(win, 0, 0);
}

int jesd_print_win(WINDOW *win, int y, int x, enum color_pairs c,
		   const char *str)
{
	wcolor_set(win, c, NULL);
	mvwprintw(win, y, x, str);
	wcolor_set(win, C_NORM, NULL);

	return jesd_get_strlen(win, x);
}

int jesd_print_win_exp(WINDOW *win, int y, int x, const char *text,
		       const char *expected, unsigned invert)
{
	enum color_pairs c;

	if (strcmp(text, expected))
		c = (invert ? C_GOOD : C_ERR);
	else
		c = (invert ? C_ERR : C_GOOD);

	return jesd_print_win(win, y, x, c, text);
}




int update_lane_status(WINDOW *win, int x, struct jesd204b_laneinfo *info,
		       unsigned lanes)
{
	struct jesd204b_laneinfo *lane;
	enum color_pairs c;
	int latency_ref, latency, i, y, pos = 0;

	if (!lanes)
		return 0;

	for (i = 0; i < lanes; i++) {
		y = 1;

		lane = info++;

		if (i == 0) {
			latency_ref = lane->k * lane->lane_latency_multiframes +
				      lane->lane_latency_octets;
			c = C_GOOD;
		} else {
			latency = lane->k * lane->lane_latency_multiframes +
				  lane->lane_latency_octets;

			if (abs(latency_ref - latency) > 8)
				c = C_ERR;
			else if (abs(latency_ref - latency) > 4)
				c = C_CRIT;
			else
				c = C_GOOD;
		}

		wcolor_set(win, C_NORM, NULL);

		mvwprintw(win, y++, x, "%d", i);
		pos = jesd_maxx(pos, jesd_get_strlen(win, x));


		if (lane->lane_errors)
			wcolor_set(win, C_ERR, NULL);
		else
			wcolor_set(win, C_GOOD, NULL);

		mvwprintw(win, y++, x, "%d", lane->lane_errors);
		pos = jesd_maxx(pos, jesd_get_strlen(win, x));

		wcolor_set(win, c, NULL);
		mvwprintw(win, y++, x, "%d/%d", lane->lane_latency_multiframes,
			  lane->lane_latency_octets);
		pos = jesd_maxx(pos, jesd_get_strlen(win, x));

		pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, lane->cgs_state, "DATA",
							0));
		pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, lane->init_frame_sync,
							"Yes", 0));
		pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, lane->init_lane_align_seq,
							"Yes", 0));

		x += pos + COL_SPACEING;
	}

	return x;
}

int jesd_update_status(WINDOW *win, int x, const char *device)
{
	struct jesd204b_jesd204_status info;
	float measured, reported, div40;
	enum color_pairs c_measured_link_clock, c_lane_rate_div;
	int y = 1, pos = 0;

	read_jesd204_status(get_full_device_path(basedir, device), &info);

	sscanf((char *)&info.measured_link_clock, "%f", &measured);
	sscanf((char *)&info.reported_link_clock, "%f", &reported);
	sscanf((char *)&info.lane_rate_div, "%f", &div40);

	if (measured > (reported * (1 + PPM(CLOCK_ACCURACY))) ||
	    measured < (reported * (1 - PPM(CLOCK_ACCURACY))))
		c_measured_link_clock = C_ERR;
	else
		c_measured_link_clock = C_GOOD;

	if (reported > (div40 * (1 + PPM(CLOCK_ACCURACY))) ||
	    reported < (div40 * (1 - PPM(CLOCK_ACCURACY))))
		c_lane_rate_div = C_ERR;
	else
		c_lane_rate_div = C_GOOD;

	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, (char *)&info.link_state,
						"enabled", 0));
	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, (char *)&info.link_status,
						"DATA", 0));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, c_measured_link_clock,
					    (char *)&info.measured_link_clock));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, C_NORM,
					    (char *)&info.reported_link_clock));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, C_NORM,
					    (char *)&info.lane_rate));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, c_lane_rate_div,
					    (char *)&info.lane_rate_div));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, C_NORM,
					    (char *)&info.lmfc_rate));
	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x,
						(char *)&info.sysref_captured, "No", 1));
	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x,
						(char *)&info.sysref_alignment_error, "Yes", 1));
	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, (char *)&info.sync_state,
						"deasserted", 0));

	return pos + COL_SPACEING;
}

int jesd_setup_subwin(WINDOW *win, const char *name, const char **lables)
{
	int i = 0, pos = 0;

	mvwprintw(win, 0, 1, name);

	while (lables[i]) {
		pos = jesd_maxx(pos, jesd_print_win(win, i + 1, 1, C_NORM, lables[i]));
		i++;
	}

	return pos + COL_SPACEING;
}

int main(int argc, char *argv[])
{
	int c, cnt = 0, x, i, simple = 0;
	int termx, termy, dev_num = 0;
	char *path = NULL;
	opterr = 0;

	while ((c = getopt(argc, argv, "sp:")) != -1)
		switch (c) {
		case 'p':
			path = optarg;
			break;
		case 's': /* Simple mode */
			simple = 1;
			break;
		case '?':
			if (optopt == 'd' || optopt == 'p')
				fprintf(stderr, "Option -%c requires an argument.\n", optopt);
			else if (isprint(optopt))
				fprintf(stderr, "Unknown option `-%c'.\n%s [-p PATH]\n",
					optopt, argv[0]);
			else
				fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);

			return 1;

		default:
			abort();
		}

	if (!path)
		path = "";

	snprintf(basedir, sizeof(basedir), "%s/sys/bus/platform/devices", path);

	dev_num = jesd_find_devices(basedir, JESD204_DEVICE_NAME, jesd_devices);
	if (!dev_num) {
		fprintf(stderr, "Failed to find JESD devices");
		return 0;
	}

	terminal_start();

	if (has_colors() /*&& COLOR_PAIRS >= 3*/) {
		start_color();
		init_pair(C_NORM, COLOR_WHITE, COLOR_BLACK);
		init_pair(C_GOOD, COLOR_GREEN, COLOR_BLACK);
		init_pair(C_ERR, COLOR_RED, COLOR_BLACK);
		init_pair(C_CRIT, COLOR_YELLOW, COLOR_BLACK);
		bkgd(COLOR_PAIR(1));
	}

	refresh();

	getmaxyx(stdscr, termy, termx);
	main_win = newwin(termy, termx, 0, 0);
	dev_win = newwin(dev_num + 3, termx - 2, 1, 1);
	stat_win = newwin(ARRAY_SIZE(link_status_lables) + 1, termx - 2, dev_num + 4,
			  1);
	lane_win = newwin(ARRAY_SIZE(lane_status_lables) + 1, termx - 2,
			  dev_num + ARRAY_SIZE(link_status_lables) + 5, 1);

	if (!simple) {
		box(dev_win, 0, 0);
		box(main_win, 0, 0);
	}

	mvwprintw(dev_win, 0, 1, "(DEVICES) Found %d JESD204 Link Layer peripherals",
		  dev_num);
	for (i = 0; i < dev_num; i++)
		mvwprintw(dev_win, 2 + i, 1, "(%d): %s", i, jesd_devices[i]);


	wrefresh(main_win);
	wrefresh(dev_win);

	while (true) {
		jesd_clear_win(stat_win, simple);
		x = jesd_setup_subwin(stat_win, "(STATUS)", link_status_lables);

		for (i = 0; i < dev_num; i++) {
			mvwprintw(stat_win, 0, x - 1, "(%d)", i);
			x += jesd_update_status(stat_win, x, jesd_devices[i]);
		}

		jesd_clear_win(lane_win, simple);
		x = jesd_setup_subwin(lane_win, "(LANE STATUS)", lane_status_lables);

		for (i = 0; i < dev_num; i++) {
			cnt = read_all_laneinfo(get_full_device_path(basedir, jesd_devices[i]), lane_info);
			if (cnt) {
				mvwprintw(lane_win, 0, x - 1, "(%d)", i);
				x = update_lane_status(lane_win, x + 1, lane_info, cnt);
			}
		}

		wrefresh(stat_win);
		wrefresh(lane_win);

		c = 0;
		c = getch();

		if (c > 0)
			break;

		sleep(1);
	}

	terminal_stop();

	return 0;
}
