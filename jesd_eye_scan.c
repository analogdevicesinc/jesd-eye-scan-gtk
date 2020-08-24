/***************************************************************************//**
 *   @file   jesd_eye_scan_gtk.c
 *   @brief  JESD204 Eye Scan Visualization Utility
 *   @author Michael Hennerich (michael.hennerich@analog.com)
********************************************************************************
 * Copyright 2014-2018 (c) Analog Devices, Inc.
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

/* HOWTO Remote:
 *  sudo sshfs -o allow_other -o sync_read root@10.44.2.224:/ /home/dave/mnt
 *  jesd_eye_scan -p /home/dave/mnt
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <linux/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <syslog.h>
#include <math.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#include <locale.h>

#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include "jesd_common.h"

#define JESD204_DEVICE_NAME 	"axi-jesd204-"
#define XCVR_DEVICE_NAME 	"axi-adxcvr-rx"

char basedir[PATH_MAX];
unsigned remote = 0;
guint timer;

GtkBuilder *builder;
GtkWidget *main_window;
GtkWidget *sock;
GtkWidget *finished_eyes;
GtkWidget *min_ber;
GtkWidget *max_ber;
GtkWidget *device_select;
GtkWidget *jesd_core_selection;
GtkWidget *xcvr_core_selection;
GtkWidget *lane[MAX_LANES];
GtkWidget *lane_status;
GtkNotebook *nbook;
GtkWidget *grid;

GtkWidget *progressbar1;

GtkWidget *tview;
GtkTextBuffer *buffer;
GtkTextIter start, end;
GtkTextIter iter;
GtkWidget *view;

GtkWidget *link_state;
GtkWidget *measured_link_clock;
GtkWidget *reported_link_clock;
GtkWidget *lane_rate;
GtkWidget *lane_rate_div;
GtkWidget *lmfc_rate;
GtkWidget *sync_state;
GtkWidget *link_status;
GtkWidget *sysref_captured;
GtkWidget *sysref_alignment_error;
GtkWidget *external_reset;

GdkColor color_red;
GdkColor color_green;
GdkColor color_orange;

pthread_t work;
GMutex *mutex;
unsigned work_run = 1;
unsigned is_first = 0;

enum {
	COLUMN = 0,
	COLUMN2,
	NUM_COLS
};

struct jesd204b_laneinfo lane_info[MAX_LANES];
struct jesd204b_xcvr_eyescan_info eyescan_info;
char jesd_devices[MAX_DEVICES][PATH_MAX];

unsigned long long get_lane_rate(unsigned lane)
{
	return lane_info[lane].fc * 1000ULL;
}

void text_view_delete(void)
{
	if (buffer != NULL) {
		gtk_text_buffer_get_start_iter(buffer, &start);
		gtk_text_buffer_get_end_iter(buffer, &end);
		gtk_text_buffer_delete(buffer, &start, &end);
		gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);
	}
}

#define JESD204_TREE_STORE_NEW_ROW_VAL(name, value)\
{\
	sprintf(temp, "%d", value);\
	gtk_tree_store_append(treestore, &child, &toplevel);\
	gtk_tree_store_set(treestore, &child, COLUMN,  name, COLUMN2, temp, -1);\
}\

#define JESD204_TREE_STORE_NEW_ROW_VALF(name, value)\
{\
	sprintf(temp, "%.3f", value);\
	gtk_tree_store_append(treestore, &child, &toplevel);\
	gtk_tree_store_set(treestore, &child, COLUMN,  name, COLUMN2, temp, -1);\
}\

#define JESD204_TREE_STORE_NEW_ROW_STRING(name, value)\
{\
	gtk_tree_store_append(treestore, &child, &toplevel);\
	gtk_tree_store_set(treestore, &child, COLUMN,  name, COLUMN2, value, -1);\
}\

static int create_and_fill_model(unsigned active_lanes)
{
	GtkTreeStore *treestore;
	GtkTreeIter toplevel, child;
	char temp[256];
	unsigned lane = 0;

	treestore = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING);

	for (lane = 0; lane < active_lanes; lane++) {
		sprintf(temp, "Lane %d", lane);
		gtk_tree_store_append(treestore, &toplevel, NULL);
		gtk_tree_store_set(treestore, &toplevel, COLUMN, temp, -1);

		JESD204_TREE_STORE_NEW_ROW_VALF("Lane Rate (Gbps)",
		                                (double)get_lane_rate(lane) /
		                                1000000000);

		JESD204_TREE_STORE_NEW_ROW_VAL("Device ID (DID)",
		                               lane_info[lane].did);
		JESD204_TREE_STORE_NEW_ROW_VAL("Bank ID (BID)",
		                               lane_info[lane].bid);
		JESD204_TREE_STORE_NEW_ROW_VAL("Lane ID (LID)",
		                               lane_info[lane].lid);

		JESD204_TREE_STORE_NEW_ROW_VAL("JESD204 Version",
		                               lane_info[lane].jesdv);
		JESD204_TREE_STORE_NEW_ROW_VAL("JESD204 subclass version",
		                               lane_info[lane].subclassv);

		JESD204_TREE_STORE_NEW_ROW_VAL("Number of Lanes per Device (L)",
		                               lane_info[lane].l);
		JESD204_TREE_STORE_NEW_ROW_VAL("Octets per Frame (F)",
		                               lane_info[lane].f);
		JESD204_TREE_STORE_NEW_ROW_VAL("Frames per Multiframe (K)",
		                               lane_info[lane].k);
		JESD204_TREE_STORE_NEW_ROW_VAL("Converters per Device (M)",
		                               lane_info[lane].m);
		JESD204_TREE_STORE_NEW_ROW_VAL("Converter Resolution (N)",
		                               lane_info[lane].n);

		JESD204_TREE_STORE_NEW_ROW_VAL("Control Bits per Sample (CS)",
		                               lane_info[lane].cs);
		JESD204_TREE_STORE_NEW_ROW_VAL
		("Samples per Converter per Frame Cycle (S)",
		 lane_info[lane].s);
		JESD204_TREE_STORE_NEW_ROW_VAL("Total Bits per Sample (N')",
		                               lane_info[lane].nd);

		JESD204_TREE_STORE_NEW_ROW_VAL
		("Control Words per Frame Cycle per Link (CF)",
		 lane_info[lane].cf);
		JESD204_TREE_STORE_NEW_ROW_STRING("Scrambling (SCR)",
		                                  lane_info[lane].
		                                  scr ? "Enabled" : "Disabled");
		JESD204_TREE_STORE_NEW_ROW_STRING("High Density Format (HD)",
		                                  lane_info[lane].
		                                  hd ? "Enabled" : "Disabled");

		JESD204_TREE_STORE_NEW_ROW_VAL("Checksum (FCHK)",
		                               lane_info[lane].fchk);
		JESD204_TREE_STORE_NEW_ROW_VAL("ADJCNT Adjustment step count",
		                               lane_info[lane].adjcnt);
		JESD204_TREE_STORE_NEW_ROW_VAL("PHYADJ Adjustment request",
		                               lane_info[lane].phyadj);
		JESD204_TREE_STORE_NEW_ROW_VAL("ADJDIR Adjustment direction",
		                               lane_info[lane].adjdir);
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(treestore));
	g_object_unref(GTK_TREE_MODEL(treestore));

	return 0;
}

static GtkWidget *create_view_and_model(unsigned active_lanes)
{
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;

	view = gtk_tree_view_new();

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Lane");
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN);

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Value");
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN2);

	return view;
}

int get_devices(const char *path, const char *driver, GtkWidget *device_select)
{
	int dev_num, i;

	dev_num = jesd_find_devices(path, driver, jesd_devices, 0);

	for (i = 0; i < dev_num; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_select),
					       (const gchar *)jesd_devices[i]);

	gtk_combo_box_set_active(GTK_COMBO_BOX(device_select), 0);

	return 0;
}

int print_output_sys(void *err, const char *str, ...)
{
	va_list args;
	char buf[250];
	int len;

	bzero(buf, 250);
	va_start(args, str);
	len = vsprintf(buf, str, args);
	va_end(args);

	if (err == stderr) {
		fprintf(stderr, buf, NULL);
		gtk_text_buffer_insert_with_tags_by_name(buffer, &iter,
		                buf, -1, "red_bg",
		                "lmarg", "bold", NULL);
	} else if (err == stdout) {
		fprintf(stdout, buf, NULL);
		gtk_text_buffer_insert_with_tags_by_name(buffer, &iter,
		                buf, -1, "bold",
		                "lmarg", NULL);
	}

	/* Fix warning: variable ‘len’ set but not used [-Wunused-but-set-variable] */
	return len;
}

static void analyse(struct jesd204b_xcvr_eyescan_info *info,
                    unsigned long long *data, unsigned int width,
                    unsigned int height, FILE *gp)
{
	unsigned *data_u32 = (unsigned *)data;
	unsigned int x, y;
	int xmin, xmax;
	int ymin, ymax;

	xmin = -1;
	xmax = -1;
	y = (height + 1) / 2;

	for (x = 0; x < width; x++) {
		if (!(info->lpm ? data_u32[y * width + x] & 0x0000FFFF :
		      data[y * width + x] & 0xFFFF0000FFFF)) {
			if (xmin == -1) {
				xmin = x;
			}

			xmax = x;
		}
	}

	ymin = -1;
	ymax = -1;
	x = (width + 1) / 2;

	for (y = 0; y < height; y++) {
		if (!(info->lpm ? data_u32[y * width + x] & 0x0000FFFF :
		      data[y * width + x] & 0xFFFF0000FFFF)) {
			if (ymin == -1) {
				ymin = y;
			}

			ymax = y;
		}
	}

	y = (height + 1) / 2;
	x = (width + 1) / 2;
	xmin -= x;
	xmax -= x;
	ymin -= y;
	ymax -= y;

	fprintf(gp, "set label 'Eye-Opening:' at -0.48,-90 front\n");
	fprintf(gp, "set label 'H: %.3f (UI)' at -0.48,-105 front\n",
	        (float)xmax / ((float)info->es_hsize) - (float)xmin / ((float)info->es_hsize));
	fprintf(gp, "set label 'V: %d (CODES)' at -0.48,-120 front\n",
	        ymax - ymin);

	print_output_sys(stdout, "   H: %.3f (UI)\n",
	                 (float)xmax / ((float)info->es_hsize)  - (float)xmin / ((float)info->es_hsize));
	print_output_sys(stdout, "   V: %d (CODES)\n", ymax - ymin);
}

double calc_ber(struct jesd204b_xcvr_eyescan_info *info,
                unsigned long long smpl, unsigned prescale)
{
	unsigned long long err_ut0, err_ut1, cnt_ut0, cnt_ut1;
	double ber;

	if (info->lpm) {
		err_ut0 = smpl & 0xFFFF;
		cnt_ut0 = (smpl >> 16) & 0xFFFF;

		if ((err_ut0) == 0) {
			ber = 1 / (double)((info->cdr_data_width << (1 + prescale)) * cnt_ut0);
		} else {
			ber = err_ut0 / (double)((info->cdr_data_width << (1 + prescale)) * cnt_ut0);
		}

	} else {
		err_ut0 = smpl & 0xFFFF;
		err_ut1 = (smpl >> 32) & 0xFFFF;
		cnt_ut0 = (smpl >> 16) & 0xFFFF;
		cnt_ut1 = (smpl >> 48) & 0xFFFF;

		if ((err_ut0 + err_ut1) == 0)
			ber =
			        1 / (double)((info->cdr_data_width << (1 + prescale)) *
			                     (cnt_ut0 + cnt_ut1));
		else
			ber = (err_ut0 * cnt_ut1 + err_ut1 * cnt_ut0) /
			      (double)(2 * (info->cdr_data_width << (1 + prescale)) * cnt_ut0 * cnt_ut1);
	}

	return ber;
}

int plot(struct jesd204b_xcvr_eyescan_info *info, char *file, unsigned lane,
         unsigned p, char *file_png)
{
	static FILE *gp = NULL;
	int ret, i, cnt;
	unsigned long long *buf;
	unsigned *buf_lpm;

	FILE *pFile;

	if (gp == NULL) {
		gp = popen("gnuplot", "w");
	}

	if (gp == NULL) {
		print_output_sys(stderr, "No Gnuplot found - Please install gnuplot-x11 !\n");
		return -1;
	}

	cnt = info->es_hsize * info->es_vsize;	/* X,Y */

	buf = malloc(cnt * (info->lpm ? 4 : 8));
	buf_lpm = (unsigned *) buf;

	if (buf == NULL) {
		exit(EXIT_FAILURE);
	}

	if (file_png == NULL) {
		/* This doesn't work for all versions of GTK/Gnuplot:
		 * https://stackoverflow.com/questions/41209199/cannot-embed-gnuplot-x11-window-into-gtk3-socket
		 */
		fprintf(gp, "set term x11 window \"%x\"\n",
		        (unsigned int)gtk_socket_get_id(GTK_SOCKET(sock)));
		fprintf(gp, "set mouse nozoomcoordinates\n");
		fprintf(gp, "set autoscale\n");
	} else {
		fprintf(gp, "set term png\n");
		fprintf(gp, "set output '%s'\n", file_png);
	}

	fprintf(gp, "set view map\n");
	fprintf(gp, "unset label\n");
	fprintf(gp, "set contour base\n");
	fprintf(gp, "set ylabel 'Vertical Offset (CODES)'\n");
	fprintf(gp, "set xlabel 'Horizontal Offset (UI)'\n");
	fprintf(gp, "set palette rgbformulae 7,5,15\n");
	fprintf(gp, "set title '"
	        "JESD204B Lane%i @ %.2f Gbps %s (Max BER %.1e)'\n",
	        lane, (double)info->lane_rate / 1000000, info->lpm ? "LPM" : "DFE",
	        calc_ber(info, 0xFFFF0000FFFF0000, p));

	fprintf(gp,
	        "set label 'Xilinx 2D Statistical Eye Scan' at graph 0.0,1.2 left front\n");

	fprintf(gp, "set grid xtics ytics front lc rgb 'grey'\n");
	fprintf(gp, "set cblabel 'BER 10E'\n");
	fprintf(gp, "set cntrparam levels incremental -1,-1,%i\n",
	        (int)log10(calc_ber(info, 0xFFFF0000FFFF0000, p)));
	fprintf(gp,
	        "set arrow from -0.175,0 to 0,22.5 nohead front lw 1 lc rgb \'white\'\n");
	fprintf(gp,
	        "set arrow from 0,22.5 to 0.175,0 nohead front lw 1 lc rgb \'white\'\n");
	fprintf(gp,
	        "set arrow from 0.175,0 to 0,-22.5 nohead front lw 1 lc rgb \'white\'\n");
	fprintf(gp,
	        "set arrow from 0,-22.5 to -0.175,0 nohead front lw 1 lc rgb \'white\'\n");
	fprintf(gp, "set label 'MASK' at 0,0 center front tc rgb 'white'\n");

	pFile = fopen(file, "r");
	ret = fread(buf, info->lpm ? 4 : 8, cnt, pFile);
	fclose(pFile);

	if (ret != cnt) {
		print_output_sys(stderr, "%s:%d: read failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	analyse(info, buf, info->es_hsize, info->es_vsize, gp);

	fprintf(gp, "splot '-' using 2:1:(log10($3)) with pm3d title ' '\n");

	fflush(gp);

	for (i = 0; i < cnt; i++) {
		if (i % info->es_hsize == 0) {
			fprintf(gp, "\n");
		}

		fprintf(gp, "%f %f %e\n",
		        ((float)(i / info->es_hsize) - (info->es_vsize / 2)),
		        ((float)(i % info->es_hsize) - (info->es_hsize / 2)) / (info->es_hsize - 1),
		        calc_ber(info, info->lpm ? buf_lpm[i] : buf[i], p));

	}

	fprintf(gp, "e\n");
	fflush(gp);

	if (buf) {
		free(buf);
	}

	return 0;
}

int write_sysfs(char *filename, char *basedir, char *val)
{
	FILE *sysfsfp;
	char temp[PATH_MAX];
	int ret = 0;

	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "w");

	if (sysfsfp == NULL) {
		return -errno;
	}

	ret = fprintf(sysfsfp, "%s", val);
	fclose(sysfsfp);

	return ret;
}

int get_eye_data(struct jesd204b_xcvr_eyescan_info *info, char *filename,
                 char *basedir, char *filename_out)
{
	FILE *sysfsfp, *pFile;
	char temp[PATH_MAX];
	unsigned long long *buf;
	int ret = 0;
	unsigned cnt = info->es_hsize * info->es_vsize;	/* X,Y */

	buf = malloc(cnt * (info->lpm ? 4 : 8));

	if (buf == NULL) {
		return -ENOMEM;
	}

	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "r");

	if (sysfsfp == NULL) {
		free(buf);
		return -errno;
	}

	ret = fread(buf, info->lpm ? 4 : 8, cnt, sysfsfp);

	fclose(sysfsfp);

	if (ret != cnt) {
		print_output_sys(stderr, "%s:%d: read failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	pFile = fopen(filename_out, "w");

	if (pFile == NULL) {
		print_output_sys(stderr, "%s:%d: open failed\n", __func__, __LINE__);
		free(buf);
		return -errno;
	}

	ret = fwrite(buf, info->lpm ? 4 : 8, cnt, pFile);
	fclose(pFile);
	free(buf);

	if (ret != cnt) {
		print_output_sys(stderr, "%s:%d: write failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	return 0;
}

int get_eye(struct jesd204b_xcvr_eyescan_info *info, unsigned lane,
            unsigned prescale)
{
	char temp[64];
	int ret;

	if (!work_run) {
		return 0;
	}

	sprintf(temp, "%d", prescale);

	write_sysfs(JESD204B_PRESCALE, info->gt_interface_path, temp);

	sprintf(temp, "%d", lane);
	write_sysfs(JESD204B_LANE_ENABLE, info->gt_interface_path, temp);

	sprintf(temp, "lane%d_p%d.eye", lane, prescale);
	ret = get_eye_data(info, JESD204B_EYE_DATA, info->gt_interface_path, temp);

	if (ret) {
		return ret;
	}

	sprintf(temp, "Lane %d : %.2e",
	        lane, calc_ber(info, 0xFFFF0000FFFF0000, prescale));

	gdk_threads_enter();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(finished_eyes),
	                               (const gchar *)temp);

	if (!is_first) {
		gtk_combo_box_set_active(GTK_COMBO_BOX(finished_eyes), 0);
		is_first++;
	}

	gdk_threads_leave();

	return 0;
}

int read_eyescan_info(const char *basedir,
                      struct jesd204b_xcvr_eyescan_info *info)
{
	FILE *pFile;
	char temp[PATH_MAX];
	int ret;

	sprintf(temp, "%s/eyescan_info", basedir);
	pFile = fopen(temp, "r");

	if (pFile == NULL) {
		print_output_sys(stderr, "Failed to read JESD204 device file: %s\n",
				 temp);
		return -errno;
	}

	ret = fscanf(pFile, "x%d,y%d CDRDW: %llu LPM: %d NL: %d LR: %lu\n",
	             &info->es_hsize, &info->es_vsize, &info->cdr_data_width,
	             &info->lpm, &info->num_lanes, &info->lane_rate);

	fclose(pFile);

	if (ret != 6) {
		print_output_sys(stderr, "Failed to read full eyescan_info\n");
		return -EINVAL;
	}

	return 0;
}

void *worker(void *args)
{
	struct jesd204b_xcvr_eyescan_info *info = args;
	unsigned lane_en = 0, p = 0, pmin, pmax, l, i = 0;

	/* get GTK thread lock */
	gdk_threads_enter();

	for (l = 0; l < MAX_LANES; l++) {
		lane_en |= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lane[l])) << l;
	}

	pmin = gtk_combo_box_get_active(GTK_COMBO_BOX(min_ber));
	pmax = gtk_combo_box_get_active(GTK_COMBO_BOX(max_ber));

	gdk_threads_leave();

	if (pmin > pmax) {
		p = pmin;
		pmin = pmax;
		pmax = p;
	}

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	for (p = pmin; p <= pmax; p++) {
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressbar1),
		                              (float)(i++) / ((pmax - pmin) ? (pmax - pmin) : 1));

		for (l = 0; l < MAX_LANES; l++)
			if (lane_en & (1 << l)) {
				get_eye(info, l, p);
			}
	}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressbar1), 1.0);

	return 0;
}

void save_plot_pressed_cb(GtkButton *button, gpointer user_data)
{
	GtkWidget *dialog;
	char temp[PATH_MAX];
	unsigned lane, prescale;
	gchar *item;
	double tmp, tmp2, places;
	unsigned int i;

	struct jesd204b_xcvr_eyescan_info *info = &eyescan_info;

	item = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(finished_eyes));

	if (item == NULL) {
		return;
	}

	sscanf(item, "Lane %d : %lf", &lane, &tmp);

	for (i = 0; i <= MAX_PRESCALE; i++) {
		tmp2 = calc_ber(info, 0xFFFF0000FFFF0000, i);
		places = pow(10.0, round(abs(log10(tmp2))));
		tmp2 = (round(tmp2 * places * 1000.0))/(places * 1000.0);

		if (tmp2 == tmp) {
			prescale = i;
			break;
		}
	}

	sprintf(item, "lane%d_p%d.eye", lane, prescale);
	sprintf(temp, "lane%d_%.2eBERT.png", lane, tmp);

	dialog = gtk_file_chooser_dialog_new("Save File",
	                                     NULL,
	                                     GTK_FILE_CHOOSER_ACTION_SAVE,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_CANCEL,
	                                     GTK_STOCK_SAVE,
	                                     GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
	                TRUE);

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), "./");
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), temp);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;

		filename =
		        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		plot(info, item, lane, prescale, filename);
		g_free(filename);

	}

	free(item);
	gtk_widget_destroy(dialog);
}

void show_pressed_cb(GtkButton *button, gpointer user_data)
{
	unsigned lane, prescale=0;
	double tmp, tmp2, places;
	unsigned int i;

	gchar *item =
	        gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT
	                        (finished_eyes));

	if (item == NULL) {
		return;
	}

	struct jesd204b_xcvr_eyescan_info *info = &eyescan_info;

	text_view_delete();

	sscanf(item, "Lane %d : %lf", &lane, &tmp);

	for (i = 0; i <= MAX_PRESCALE; i++) {
		tmp2 = calc_ber(info, 0xFFFF0000FFFF0000, i);
		places = pow(10.0, round(abs(log10(tmp2))));
		tmp2 = (round(tmp2 * places * 1000.0))/(places * 1000.0);

		if (tmp2 == tmp) {
			prescale = i;
			break;
		}
	}

	sprintf(item, "lane%d_p%d.eye", lane, prescale);

	print_output_sys(stdout, "LANE%d P(%d) @ %.2f Gbps\n", lane, prescale,
	                 (double)info->lane_rate / 1000000);
	print_output_sys(stdout, "Eye Center:\n  ERR: 0 BER: %.3e\n",
	                 calc_ber(info, 0xFFFF0000FFFF0000, prescale));

	plot(info, item, lane, prescale, NULL);
	free(item);
}

void start_pressed_cb(GtkButton *button, gpointer user_data)
{
	struct jesd204b_xcvr_eyescan_info *info = &eyescan_info;
	int ret;

	if (work) {
		ret = pthread_tryjoin_np(work, NULL);
		if (ret) {
			print_output_sys(stderr, "Wait until previous run terminates\n");
			return;
		}
	}

	ret = read_eyescan_info(info->gt_interface_path, info);

	if (ret) {
		return;
	}

	work_run = 1;
	gtk_list_store_clear(GTK_LIST_STORE
	                     (gtk_combo_box_get_model
	                      (GTK_COMBO_BOX(finished_eyes))));
	is_first = 0;
	pthread_create(&work, NULL, worker, info);
}

void terminate_pressed_cb(GtkButton *button, gpointer user_data)
{
	work_run = 0;
	if (work) {
		pthread_cancel(work);
	}
}

void device_select_pressed_cb(GtkComboBoxText *combo_box, gpointer user_data)
{
	int i, ret;
	char temp[128];
	struct jesd204b_xcvr_eyescan_info *info = &eyescan_info;

	work_run = 0;

	gchar *item = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));

	if (item == NULL) {
		return;
	}

	ret = snprintf(info->gt_interface_path, sizeof(info->gt_interface_path),
	               "%s/%s", basedir, item);

	if (ret < 0) {
		return;
	}

	gtk_list_store_clear(GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(
	                finished_eyes))));
	text_view_delete();

	ret = read_eyescan_info(info->gt_interface_path, info);

	if (ret) {
		return;
	}

	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(min_ber));
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(max_ber));

	/* Populate min/max BER combo boxes */
	for (i = 0; i <= MAX_PRESCALE; i++) {
		sprintf(temp, "%.2e",
		        calc_ber(info, 0xFFFF0000FFFF0000, i));
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(min_ber),
		                               (const gchar *)temp);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(max_ber),
		                               (const gchar *)temp);

		if (i == 0) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(min_ber), 0);
			gtk_combo_box_set_active(GTK_COMBO_BOX(max_ber), 0);

		}
	}

	/* Hide lane enable check boxes */
	for (i = 0; i < MAX_LANES; i++) {
		if (i < info->num_lanes) {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lane[i]), TRUE);
			gtk_widget_show(lane[i]);
		} else {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lane[i]), FALSE);
			gtk_widget_hide(lane[i]);
		}
	}
}

GtkWidget *set_lable_text(GtkWidget *label, const char *text,
                          const char *expected, unsigned invert)
{
	GdkColor color;

	if (g_strcmp0(text, expected)) {
		color = (invert ? color_green : color_red);
	} else {
		color = (invert ? color_red: color_green);
	}

	if (label) {
		gtk_label_set_text(GTK_LABEL(label), text);
	} else {
		label = gtk_label_new(text);
	}

	if (expected != NULL) {
		gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &color);
	}

	return label;
}

void my_gtk_label_set_xalign(GtkLabel *label, float xalign)
{
	GValue val = G_VALUE_INIT;
	g_value_init(&val, G_TYPE_FLOAT);
	g_value_set_float(&val, xalign);
	g_object_set_property(G_OBJECT(label), "xalign", &val);
	g_value_unset(&val);
}

GtkWidget *set_per_lane_status(struct jesd204b_laneinfo *info, unsigned lanes)
{
	struct jesd204b_laneinfo *lane;
	GdkColor color;

	GtkWidget *label;
	char text[128];
	int i, j;
	int latency_min, latency, octets_per_multifame;
	struct jesd204b_laneinfo *tmp = info;

	static const char *tab_lables[] = {
		"Lane#",
		"Errors",
		"Latency \n(Multiframes/Octets)",
		"CGS State",
		"Initial Frame Sync",
		"Initial Lane \nAlignment Sequence",
	};

	if (grid) {
		gtk_widget_destroy(GTK_WIDGET(grid));
		grid = NULL;
	}

	if (lanes == 0) {
		return NULL;
	}

	for (i = 0, latency_min = INT_MAX; i < lanes; i++) {
		lane = tmp++;
		octets_per_multifame = lane->k * lane->f;

		latency_min = MIN(latency_min, octets_per_multifame *
			lane->lane_latency_multiframes +
			lane->lane_latency_octets);
	}

	grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 10);

	for (i = 0; i < lanes + 1; i++) {
		j = 0;

		if (i == 0) {
			for (j = 0; j < 6; j++) {
				label = set_lable_text(NULL, tab_lables[j], NULL, 0);
				my_gtk_label_set_xalign(GTK_LABEL(label), 0);
				gtk_grid_attach(GTK_GRID(grid), label, i, j, 1, 1);
			}
		} else {
			lane = info++;
			octets_per_multifame = lane->k * lane->f;

			latency = octets_per_multifame * lane->lane_latency_multiframes +
				  lane->lane_latency_octets;

			if ((latency - latency_min) >= octets_per_multifame) {
				color = color_red;
			} else {
				if ((latency - latency_min) > (octets_per_multifame / 2)) {
					color = color_orange;
				} else {
					color = color_green;
				}
			}

			g_snprintf(text, sizeof(text), "Lane %d", i);
			label = set_lable_text(NULL, text, NULL, 0);
			gtk_grid_attach(GTK_GRID(grid), label, i, j++, 1, 1);

			g_snprintf(text, sizeof(text), "%d", lane->lane_errors);
			label = set_lable_text(NULL, text, "0", 0);
			gtk_grid_attach(GTK_GRID(grid), label, i, j++, 1, 1);

			g_snprintf(text, sizeof(text), "%d / %d", lane->lane_latency_multiframes,
			           lane->lane_latency_octets);
			label = set_lable_text(NULL, text, NULL, 0);
			gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &color);
			gtk_grid_attach(GTK_GRID(grid), label, i, j++, 1, 1);

			label = set_lable_text(NULL, lane->cgs_state, "DATA", 0);
			gtk_grid_attach(GTK_GRID(grid), label, i, j++, 1, 1);

			label = set_lable_text(NULL, lane->init_frame_sync, "Yes", 0);
			gtk_grid_attach(GTK_GRID(grid), label, i, j++, 1, 1);

			label = set_lable_text(NULL, lane->init_lane_align_seq, "Yes", 0);
			gtk_grid_attach(GTK_GRID(grid), label, i, j++, 1, 1);
		}
	}

	gtk_container_add(GTK_CONTAINER(lane_status), grid);
	gtk_widget_show_all(grid);

	return grid;
}



void jesd_update_status(const char *path)
{
	struct jesd204b_jesd204_status info;
	float measured, reported, div40;
	GdkColor color;

	read_jesd204_status(path, &info);

	set_lable_text(link_state,(char *) &info.link_state, "enabled", 0);
	set_lable_text(link_status, (char *)&info.link_status, "DATA", 0);
	set_lable_text(measured_link_clock, (char *)&info.measured_link_clock, NULL, 0);
	set_lable_text(reported_link_clock, (char *)&info.reported_link_clock, NULL, 0);
	set_lable_text(lane_rate, (char *)&info.lane_rate, NULL, 0);
	set_lable_text(lane_rate_div,(char *) &info.lane_rate_div, NULL, 0);
	set_lable_text(lmfc_rate,(char *) &info.lmfc_rate, NULL, 0);

	set_lable_text(sync_state, (char *)&info.sync_state, "deasserted", 0);
	set_lable_text(sysref_captured, (char *)&info.sysref_captured, "No", 1);
	set_lable_text(sysref_alignment_error, (char *)&info.sysref_alignment_error,
	               "Yes", 1);

	sscanf((char *)&info.measured_link_clock, "%f", &measured);
	sscanf((char *)&info.reported_link_clock, "%f", &reported);
	sscanf((char *)&info.lane_rate_div, "%f", &div40);

	if (measured > (reported * (1 + PPM(CLOCK_ACCURACY))) ||
		measured < (reported * (1 - PPM(CLOCK_ACCURACY)))) {
		color = color_red;
	} else {
		color = color_green;
	}

	gtk_widget_modify_fg(measured_link_clock, GTK_STATE_NORMAL, &color);

	if (reported > (div40 * (1 + PPM(CLOCK_ACCURACY))) ||
		reported < (div40 * (1 - PPM(CLOCK_ACCURACY)))) {
		color = color_red;
	} else {
		color = color_green;
	}

	gtk_widget_modify_fg(lane_rate_div, GTK_STATE_NORMAL, &color);
}

static int update_status(GtkComboBoxText *combo_box)
{
	int cnt = 0;
	char *path;

	gchar *item = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_box));

	if (item == NULL) {
		return cnt;
	}

	g_mutex_lock(mutex);
	path = get_full_device_path(basedir, item);
	jesd_update_status(path);
	cnt = read_all_laneinfo(path, lane_info);
	grid = set_per_lane_status(lane_info, cnt);
	g_mutex_unlock(mutex);

	return cnt;
}

static gboolean update_page(void)
{
	gint page = gtk_notebook_get_current_page(nbook);

	if (page == 0) {
		update_status(GTK_COMBO_BOX_TEXT(jesd_core_selection));
	}

	return TRUE;
}

void jesd_core_selection_cb(GtkComboBoxText *combo_box, gpointer user_data)
{
	create_and_fill_model(update_status(combo_box));
}

int main(int argc, char *argv[])
{
	GtkWidget *box2;
	GtkWidget *box3;
	GtkWidget *view;
	GtkImage *logo;
	struct stat buf;
	int ret, c, i, cnt = 0;
	char *path = NULL;
	opterr = 0;

	while ((c = getopt(argc, argv, "p:")) != -1)
		switch (c) {
		case 'p':
			path = optarg;
			remote = 1;
			break;

		case '?':
			if (optopt == 'd'|| optopt == 'p') {
				fprintf(stderr, "Option -%c requires an argument.\n", optopt);
			} else if (isprint(optopt))
				fprintf(stderr, "Unknown option `-%c'.\n%s [-p PATH] [-d DEVICEINDEX]\n",
				        optopt, argv[0]);
			else
				fprintf(stderr,
				        "Unknown option character `\\x%x'.\n",
				        optopt);

			return 1;

		default:
			abort();
		}

	if (!path || !remote) {
		path = "";
	}

	ret = snprintf(basedir, sizeof(basedir), "%s/sys/bus/platform/drivers", path);

	if (ret < 0) {
		return EXIT_FAILURE;
	}


	setlocale(LC_NUMERIC, "C");
	mutex = g_mutex_new();

	/* init threads */
	gdk_threads_init();
	gdk_threads_enter();

	gtk_init(&argc, &argv);

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "./jesd.glade", NULL)) {
		gtk_builder_add_from_file(builder, "/usr/local/share/jesd/jesd.glade", NULL);
	}

	main_window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

	nbook = GTK_NOTEBOOK(gtk_builder_get_object(builder, "notebook1"));


	for (i = 0; i < MAX_LANES; i++) {
		char text[32];
		g_snprintf(text, sizeof(text), "checkbutton%d", i + 1);
		lane[i] = GTK_WIDGET(gtk_builder_get_object(builder, text));
	}

	gdk_color_parse("red", &color_red);
	gdk_color_parse("green", &color_green);
	gdk_color_parse("orange", &color_orange);

	link_state = GTK_WIDGET(gtk_builder_get_object(builder, "link_state"));
	measured_link_clock = GTK_WIDGET(gtk_builder_get_object(builder,
	                                 "measured_link_clock"));
	reported_link_clock = GTK_WIDGET(gtk_builder_get_object(builder,
	                                 "reported_link_clock"));
	lane_rate = GTK_WIDGET(gtk_builder_get_object(builder, "lane_rate"));
	lane_rate_div = GTK_WIDGET(gtk_builder_get_object(builder, "lane_rate_div"));
	lmfc_rate = GTK_WIDGET(gtk_builder_get_object(builder, "lmfc_rate"));
	sync_state = GTK_WIDGET(gtk_builder_get_object(builder, "sync_state"));
	link_status = GTK_WIDGET(gtk_builder_get_object(builder, "link_status"));
	sysref_captured = GTK_WIDGET(gtk_builder_get_object(builder,
	                             "sysref_captured"));
	sysref_alignment_error = GTK_WIDGET(gtk_builder_get_object(builder,
	                                    "sysref_alignment_error"));

	tview = GTK_WIDGET(gtk_builder_get_object(builder, "textview1"));

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tview));
	gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);
	gtk_text_buffer_create_tag(buffer, "lmarg", "left_margin", 5, NULL);
	gtk_text_buffer_create_tag(buffer, "blue_fg", "foreground", "blue",
	                           NULL);
	gtk_text_buffer_create_tag(buffer, "red_bg", "background", "red", NULL);
	gtk_text_buffer_create_tag(buffer, "italic", "style",
	                           PANGO_STYLE_ITALIC, NULL);
	gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD,
	                           NULL);

	progressbar1 =
	        GTK_WIDGET(gtk_builder_get_object(builder, "progressbar1"));

	box2 = GTK_WIDGET(gtk_builder_get_object(builder, "box2"));
	sock = gtk_socket_new();
	gtk_widget_set_hexpand(sock, TRUE);
	gtk_widget_set_halign(sock, GTK_ALIGN_FILL);
	gtk_widget_set_vexpand(sock, TRUE);
	gtk_widget_set_valign(sock, GTK_ALIGN_FILL);
	gtk_widget_show(sock);
	gtk_container_add(GTK_CONTAINER(box2), sock);
	gtk_widget_set_size_request(GTK_WIDGET(sock), 480, 360);

	box3 = GTK_WIDGET(gtk_builder_get_object(builder, "jesd_info"));
	view = create_view_and_model(cnt);
	gtk_container_add(GTK_CONTAINER(box3), view);

	finished_eyes =
	        GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext1"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(finished_eyes), 0);

	min_ber = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext2"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(min_ber), 0);

	max_ber = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext3"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(max_ber), 0);

	device_select = GTK_WIDGET(gtk_builder_get_object(builder,
	                           "comboboxtext_device_select"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(device_select), 0);

	jesd_core_selection = GTK_WIDGET(gtk_builder_get_object(builder,
	                                 "jesd_core_selection"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(jesd_core_selection), 0);

	lane_status = GTK_WIDGET(gtk_builder_get_object(builder, "lane_status"));

	gtk_builder_connect_signals(builder, NULL);

	g_signal_connect(G_OBJECT(main_window), "destroy",
	                 G_CALLBACK(gtk_main_quit), NULL);

	get_devices(basedir, XCVR_DRIVER_NAME, device_select);
	get_devices(basedir, JESD204_RX_DRIVER_NAME, jesd_core_selection);
	get_devices(basedir, JESD204_TX_DRIVER_NAME, jesd_core_selection);

	logo = GTK_IMAGE(gtk_builder_get_object(builder, "logo"));

	if (!stat("./icons/ADIlogo.png", &buf)) {
		g_object_set(logo, "file", "./icons/ADIlogo.png", NULL);
	} else {
		g_object_set(logo, "file", "/usr/local/share/jesd/ADIlogo.png", NULL);
	}

	gtk_widget_show_all(main_window);

	timer = g_timeout_add(1000, (GSourceFunc) update_page, NULL);

	/* enter the GTK main loop */
	gtk_main();
	gdk_threads_leave();

	return 0;
}
