/***************************************************************************//**
*   @file   jesd_status.c
*   @brief  JESD204 Status Information Utility
*   @author Michael Hennerich (michael.hennerich@analog.com)
********************************************************************************
* Copyright 2014-2025(c) Analog Devices, Inc. All rights reserved.
*
* An ADI specific BSD license, which can be found in the top level directory
* of this repository (LICENSE.txt), and also on-line at:
* https://github.com/analogdevicesinc/jesd-eye-scan-gtk/blob/main/LICENSE.txt
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
#include <limits.h>

#include "jesd_common.h"

#define COL_SPACEING 3

enum color_pairs {
	C_NORM = 1,
	C_GOOD = 2,
	C_ERR = 3,
	C_CRIT = 4,
	C_OPT = 5,
};

static int encoder = 0;
struct jesd204b_laneinfo lane_info[MAX_LANES];
char basedir[PATH_MAX];
char jesd_devices[MAX_DEVICES][PATH_MAX];
WINDOW *main_win, *stat_win, *dev_win, *lane_win;

static const char *link_status_labels[] = {
	"Link is",
	"Link Status",
	"Measured Link Clock (MHz)",
	"Reported Link Clock (MHz)",
	"Measured Device Clock (MHz)",
	"Reported Device Clock (MHz)",
	"Desired Device Clock (MHz)",
	"Lane rate (MHz)",
	"Lane rate / 40 (MHz)",
	"LMFC rate (MHz)",
	"SYSREF captured",
	"SYSREF alignment error",
	"SYNC~",
	NULL
};

static const char *link_status_labels_64b66b[] = {
	"Link is",
	"Link Status",
	"Measured Link Clock (MHz)",
	"Reported Link Clock (MHz)",
	"Measured Device Clock (MHz)",
	"Reported Device Clock (MHz)",
	"Desired Device Clock (MHz)",
	"Lane rate (MHz)",
	"Lane rate / 66 (MHz)",
	"LEMC rate (MHz)",
	"SYSREF captured",
	"SYSREF alignment error",
	NULL
};

static const char *lane_status_labels[] = {
	"Lane#",
	"Errors",
	"Latency (Multiframes/Octets)",
	"CGS State",
	"Initial Frame Sync",
	"Initial Lane Alignment Sequence",
	NULL
};

static const char *lane_status_labels_64b66b[] = {
	"Lane#",
	"Errors",
	"Latency (Octets)",
	"Extended multiblock alignment",
	NULL
};


static void jesd_clear_line_from(WINDOW *win, int y, int x)
{
	wmove(win, y, x);
	wclrtoeol(win);
}

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
	(void)y1;  /* Suppress unused variable warning */

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

#define jesd_print_win_args(win, y, x, c, fmt, args...) \
({ \
	int __x; \
	\
	wcolor_set(win, c, NULL); \
	mvwprintw(win, y, x, fmt, ##args); \
	wcolor_set(win, C_NORM, NULL); \
	__x = jesd_get_strlen(win, x); \
	__x; \
})


int jesd_print_win(WINDOW *win, int y, int x, enum color_pairs c,
		   const char *str, const bool clear)
{
	if (clear)
		jesd_clear_line_from(win, y, x);

	wcolor_set(win, c, NULL);
	mvwprintw(win, y, x, "%s", str);
	wcolor_set(win, C_NORM, NULL);

	return jesd_get_strlen(win, x);
}

int jesd_print_win_exp(WINDOW *win, int y, int x, const char *text,
		       const char *expected, unsigned invert, const bool clear)
{
	enum color_pairs c;

	if (strcmp(text, expected))
		c = (invert ? C_GOOD : C_ERR);
	else
		c = (invert ? C_ERR : C_GOOD);

	return jesd_print_win(win, y, x, c, text, clear);
}

int update_lane_status(WINDOW *win, int x, struct jesd204b_laneinfo *info,
		       unsigned lanes)
{
	struct jesd204b_laneinfo *lane;
	enum color_pairs c = C_ERR;
	int octets_per_multifame, latency_min, latency, i, y, pos = 0;
	struct jesd204b_laneinfo *tmp = info;
	bool not_available = false;

	if (!lanes)
		return 0;

	for (i = 0, latency_min = INT_MAX; i < lanes; i++) {
		lane = tmp++;
		octets_per_multifame = lane->k * lane->f;

		latency_min = MIN(latency_min, octets_per_multifame *
				  lane->lane_latency_multiframes +
				  lane->lane_latency_octets);
	}

	for (i = 0; i < lanes; i++) {
		y = 1;

		lane = info++;

		if (encoder == JESD204_ENCODER_64B66B) {
			if (lane->lane_latency_octets == 0 && lane->lane_latency_min == 0 && lane->lane_latency_max == 0) {
				c = C_GOOD;
				not_available = true;
			} else if (lane->lane_latency_octets < lane->lane_latency_min || lane->lane_latency_octets > lane->lane_latency_max) {
				c = C_ERR;
			} else {
				c = C_GOOD;
			}
		} else {
			octets_per_multifame = lane->k * lane->f;

			latency = octets_per_multifame * lane->lane_latency_multiframes +
				  lane->lane_latency_octets;

			if ((latency - latency_min) >= octets_per_multifame)
				c = C_ERR;
			else if ((latency - latency_min) > (octets_per_multifame / 2))
				c = C_CRIT;
			else
				c = C_GOOD;
		}

		wcolor_set(win, C_NORM, NULL);

		jesd_clear_line_from(win, y, x);
		mvwprintw(win, y++, x, "%d", i);
		pos = jesd_maxx(pos, jesd_get_strlen(win, x));


		if (lane->lane_errors)
			wcolor_set(win, C_ERR, NULL);
		else
			wcolor_set(win, C_GOOD, NULL);

		jesd_clear_line_from(win, y, x);
		mvwprintw(win, y++, x, "%d", lane->lane_errors);
		pos = jesd_maxx(pos, jesd_get_strlen(win, x));

		if (encoder == JESD204_ENCODER_64B66B) {
			wcolor_set(win, c, NULL);
			jesd_clear_line_from(win, y, x);
			if (not_available) {
				mvwprintw(win, y++, x, "N/A");
			} else {
				mvwprintw(win, y++, x, "%u", lane->lane_latency_octets);
			}
			pos = jesd_maxx(pos, jesd_get_strlen(win, x));

			pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x,
								lane->ext_multiblock_align_state,
								"EMB_LOCK", 0, true));
			x += pos + COL_SPACEING;
			continue;
		}

		wcolor_set(win, c, NULL);
		jesd_clear_line_from(win, y, x);
		mvwprintw(win, y++, x, "%d/%d", lane->lane_latency_multiframes,
			  lane->lane_latency_octets);
		pos = jesd_maxx(pos, jesd_get_strlen(win, x));

		pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, lane->cgs_state, "DATA",
							0, true));
		pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, lane->init_frame_sync,
							"Yes", 0, true));
		pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, lane->init_lane_align_seq,
							"Yes", 0, true));

		x += pos + COL_SPACEING;
	}

	return x;
}

int jesd_update_status(WINDOW *win, int x, const char *device)
{
	struct jesd204b_jesd204_status info;
	float measured, reported, div40;
	enum color_pairs c_measured_link_clock, c_lane_rate_div,
	     c_measured_device_clock, c_reported_device_clock;
	int y = 1, pos = 0;

	if (g_jesd_iio_ctx) {
		jesd_read_jesd204_status(device, &info);
	} else {
		char *path = get_full_device_path(basedir, device);
		if (!path)
			return -1;
		jesd_read_jesd204_status(path, &info);
		free(path);
	}

	if (sscanf((char *)&info.measured_link_clock, "%f", &measured) != 1)
		measured = 0.0f;
	if (sscanf((char *)&info.reported_link_clock, "%f", &reported) != 1)
		reported = 0.0f;
	if (sscanf((char *)&info.lane_rate_div, "%f", &div40) != 1)
		div40 = 0.0f;

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

	if (info.measured_device_clock[0] != 'N') {
		if (sscanf((char *)&info.measured_device_clock, "%f", &measured) != 1)
			measured = 0.0f;
		if (sscanf((char *)&info.reported_device_clock, "%f", &reported) != 1)
			reported = 0.0f;
		if (sscanf((char *)&info.desired_device_clock, "%f", &div40) != 1)
			div40 = 0.0f;

		if (measured > (reported * (1 + PPM(CLOCK_ACCURACY))) ||
		    measured < (reported * (1 - PPM(CLOCK_ACCURACY))))
			c_measured_device_clock = C_ERR;
		else
			c_measured_device_clock = C_GOOD;

		if (reported > (div40 * (1 + PPM(CLOCK_ACCURACY))) ||
		    reported < (div40 * (1 - PPM(CLOCK_ACCURACY))))
			c_reported_device_clock = C_ERR;
		else
			c_reported_device_clock = C_GOOD;
	} else {
		c_measured_device_clock = C_NORM;
		c_reported_device_clock = C_NORM;
	}

	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, (char *)&info.link_state,
						"enabled", 0, true));
	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, (char *)&info.link_status,
						"DATA", 0, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, c_measured_link_clock,
					    (char *)&info.measured_link_clock, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, C_NORM,
					    (char *)&info.reported_link_clock, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, c_measured_device_clock,
					    (char *)&info.measured_device_clock, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, c_reported_device_clock,
					    (char *)&info.reported_device_clock, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, C_NORM,
					    (char *)&info.desired_device_clock, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, C_NORM,
					    (char *)&info.lane_rate, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, c_lane_rate_div,
					    (char *)&info.lane_rate_div, true));
	pos = jesd_maxx(pos, jesd_print_win(win, y++, x, C_NORM,
					    (char *)&info.lmfc_rate, true));
	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x,
						(char *)&info.sysref_captured, "No", 1, true));
	pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x,
						(char *)&info.sysref_alignment_error, "Yes", 1, true));
	if (encoder == JESD204_ENCODER_8B10B)
		pos = jesd_maxx(pos, jesd_print_win_exp(win, y++, x, (char *)&info.sync_state,
							"deasserted", 0, true));

	return pos + COL_SPACEING;
}

int jesd_setup_subwin(WINDOW *win, const char *name, const char **labels)
{
	int i = 0, pos = 0;

	mvwprintw(win, 0, 1, "%s", name);

	while (labels[i]) {
		pos = jesd_maxx(pos, jesd_print_win(win, i + 1, 1, C_NORM, labels[i], false));
		i++;
	}

	return pos + COL_SPACEING;
}

static void jesd_set_current_device(const int dev_idx)
{
	wcolor_set(dev_win, C_GOOD, NULL);
	mvwprintw(dev_win, 2 + dev_idx, strlen(jesd_devices[dev_idx]) + 8, "[*]");
	wcolor_set(dev_win, C_NORM, NULL);
	wrefresh(dev_win);
}

/*
 * This is a workaround to redraw the right line in the windows boxes/borders.
 * This happens because `jesd_clear_line_from()` will clear the current line
 * till the EOL which removes part of the box vertical line.
 */
static void jesd_redo_r_box(WINDOW *win, const int simple)
{
	int x, y;

	if (simple)
		return;

	getmaxyx(win, y, x);
	wmove(win, 1, x - 1);
	/* redraw vertical line */
	wvline(win, 0, y - 2);
}

static void jesd_move_device(const int old_idx, const int new_idx,
			     const int simple)
{
	if (old_idx == new_idx)
		return;
	/* clear the old selected dev */
	jesd_clear_line_from(dev_win, 2 + old_idx, strlen(jesd_devices[old_idx]) + 6);
	jesd_redo_r_box(dev_win, simple);
	jesd_set_current_device(new_idx);
	/* Clear the lane window since the next device might not have lane info */
	jesd_clear_win(lane_win, true);
	wrefresh(lane_win);
	jesd_clear_win(stat_win, simple);
}

int main(int argc, char *argv[])
{
	int c, cnt = 0, x = 1, i, simple = 0;
	int up_key = 'a', down_key = 'd';
	int termx, termy, dev_num = 0;
	char *path = NULL;
	char *uri = NULL;
	int dev_idx = 0;

	opterr = 0;

	while ((c = getopt(argc, argv, "svp:u:")) != -1)
		switch (c) {
		case 'p':
			path = optarg;
			break;
		case 's': /* Simple mode */
			simple = 1;
			break;
		case 'v':
			up_key = 'k';
			down_key = 'j';
			break;
		case 'u':
			uri = optarg;
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

	if (uri || (path && strlen(path) == 0)) {
		/* Use libiio */
		g_jesd_iio_ctx = jesd_iio_create_context(uri);
		if (!g_jesd_iio_ctx) {
			fprintf(stderr, "Failed to create IIO context\n");
			return 1;
		}
		dev_num = jesd_iio_find_devices(g_jesd_iio_ctx, jesd_devices);
		strcpy(basedir, "iio:context");
	} else {
		/* Fall back to sysfs */
		snprintf(basedir, sizeof(basedir), "%s/sys/bus/platform/drivers", path);
		dev_num = jesd_find_devices(basedir, JESD204_RX_DRIVER_NAME, "status", jesd_devices, 0);
		dev_num = jesd_find_devices(basedir, JESD204_TX_DRIVER_NAME, "status", jesd_devices, dev_num);
	}
	if (!dev_num) {
		fprintf(stderr, "Failed to find JESD devices\n");
		return 0;
	}

	terminal_start();

	if (has_colors() /*&& COLOR_PAIRS >= 3*/) {
		start_color();
		init_pair(C_NORM, COLOR_WHITE, COLOR_BLACK);
		init_pair(C_GOOD, COLOR_GREEN, COLOR_BLACK);
		init_pair(C_ERR, COLOR_RED, COLOR_BLACK);
		init_pair(C_CRIT, COLOR_YELLOW, COLOR_BLACK);
		init_pair(C_OPT, COLOR_WHITE, COLOR_CYAN);
		bkgd(COLOR_PAIR(1));
	}

	refresh();

	getmaxyx(stdscr, termy, termx);
	main_win = newwin(termy, termx, 0, 0);
	dev_win = newwin(dev_num + 3, termx - 2, 1, 1);
	stat_win = newwin(ARRAY_SIZE(link_status_labels) + 1, termx - 2, dev_num + 4,
			  1);
	lane_win = newwin(ARRAY_SIZE(lane_status_labels) + 1, termx - 2,
			  dev_num + ARRAY_SIZE(link_status_labels) + 5, 1);

	if (!simple) {
		box(dev_win, 0, 0);
		box(main_win, 0, 0);
		box(stat_win, 0, 0);
		box(lane_win, 0, 0);
	}

	mvwprintw(dev_win, 0, 1, "(DEVICES) Found %d JESD204 Link Layer peripherals",
		  dev_num);

	for (i = 0; i < dev_num; i++) {
		mvwprintw(dev_win, 2 + i, 1, "(%d): %s", i, jesd_devices[i]);
		x += jesd_print_win_args(main_win, termy - 2, x, C_NORM, "F%d", i + 1);
		x += jesd_print_win_args(main_win, termy - 2, x, C_OPT, "%s", jesd_devices[i]);
	}
	/* add quit option */
	x += jesd_print_win_args(main_win, termy - 2, x, C_NORM, "F%d",
				 MAX_DEVICES + 1);
	jesd_print_win(main_win, termy - 2, x, C_OPT, "Quit", false);

	jesd_print_win(main_win, termy - 3, 1, C_OPT,
		       "You can also use 'q' to quit and 'a' or 'd' to move between devices!",
		       false);

	wrefresh(main_win);
	wrefresh(dev_win);

	/* defaut to first device */
	jesd_set_current_device(0);

	while (true) {
		char *path = NULL;

		if (g_jesd_iio_ctx) {
			encoder = jesd_read_encoding(jesd_devices[dev_idx]);
		} else {
			path = get_full_device_path(basedir, jesd_devices[dev_idx]);
			if (!path)
				continue;
			encoder = jesd_read_encoding(path);
		}

		if (encoder == JESD204_ENCODER_8B10B)
			x = jesd_setup_subwin(stat_win, "(STATUS)", link_status_labels);
		else
			x = jesd_setup_subwin(stat_win, "(STATUS)", link_status_labels_64b66b);

		jesd_update_status(stat_win, x, jesd_devices[dev_idx]);
		jesd_redo_r_box(stat_win, simple);

		if (g_jesd_iio_ctx) {
			cnt = jesd_read_all_laneinfo(jesd_devices[dev_idx], lane_info);
		} else {
			if (!path) {
				path = get_full_device_path(basedir, jesd_devices[dev_idx]);
				if (!path)
					continue;
			}
			cnt = jesd_read_all_laneinfo(path, lane_info);
		}

		if (path) {
			free(path);
			path = NULL;
		}
		if (cnt) {
			if (!simple)
				box(lane_win, 0, 0);

			if (encoder == JESD204_ENCODER_8B10B)
				x = jesd_setup_subwin(lane_win, "(LANE STATUS)", lane_status_labels);
			else
				x = jesd_setup_subwin(lane_win, "(LANE STATUS)", lane_status_labels_64b66b);

			update_lane_status(lane_win, x + 1, lane_info, cnt);
			jesd_redo_r_box(lane_win, simple);
			wrefresh(lane_win);
		}

		wrefresh(stat_win);
		usleep(1000 * 250); /* 250 ms */

		c = getch();
		if (c >= KEY_F(1) && c <= KEY_F0 + dev_num) {
			int old_idx = dev_idx;
			dev_idx = c - KEY_F0 - 1;
			jesd_move_device(old_idx, dev_idx, simple);
		} else if (c == down_key) {
			int old_idx = dev_idx;

			if (dev_idx + 1 >= dev_num)
				dev_idx = 0;
			else
				dev_idx++;
			jesd_move_device(old_idx, dev_idx, simple);
		} else if (c == up_key) {
			int old_idx = dev_idx;
			if (!dev_idx)
				dev_idx = dev_num - 1;
			else
				dev_idx--;
			jesd_move_device(old_idx, dev_idx, simple);
		} else if (c == KEY_F0 + MAX_DEVICES + 1 || c == 'q')
			break;

	}

	terminal_stop();

	if (g_jesd_iio_ctx)
		jesd_iio_destroy_context(g_jesd_iio_ctx);

	return 0;
}
