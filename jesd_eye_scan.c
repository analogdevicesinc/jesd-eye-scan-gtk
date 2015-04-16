/***************************************************************************//**
 *   @file   jesd_eye_scan_gtk.c
 *   @brief  JESD204 Eye Scan Visualization Utility
 *   @author Michael Hennerich (michael.hennerich@analog.com)
********************************************************************************
 * Copyright 2014(c) Analog Devices, Inc.
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

char jesd_interface_path[PATH_MAX];
char gt_interface_path[PATH_MAX];

#define JESD204B_DEV_PATH jesd_interface_path
#define JESD204B_GT_DEV_PATH gt_interface_path

#define JESD204B_LANE_ENABLE	"enable"
#define JESD204B_PRESCALE	"prescale"
#define JESD204B_EYE_DATA	"eye_data"

#define MAX_LANES		8
#define MAX_PRESCALE		31

unsigned es_hsize;
unsigned es_vsize;
unsigned cdr_data_width;
unsigned new_interface = 0;
unsigned remote = 0;
unsigned lpm = 0;


GtkBuilder *builder;
GtkWidget *main_window;
GtkWidget *sock;
GtkWidget *finished_eyes;
GtkWidget *min_ber;
GtkWidget *max_ber;
GtkWidget *lane[MAX_LANES];

GtkWidget *progressbar1;

GtkWidget *tview;
GtkTextBuffer *buffer;
GtkTextIter start, end;
GtkTextIter iter;

pthread_t work;
unsigned work_run = 1;
unsigned is_first = 0;

enum {
	COLUMN = 0,
	COLUMN2,
	NUM_COLS
};

struct jesd204b_laneinfo {
	unsigned did;		/* DID Device ID */
	unsigned bid;		/* BID Bank ID */
	unsigned lid;		/* LID Lane ID */
	unsigned l;		/* Number of Lanes per Device */
	unsigned scr;		/* SCR Scrambling Enabled */
	unsigned f;		/* Octets per Frame */
	unsigned k;		/* Frames per Multiframe */
	unsigned m;		/* Converters per Device */
	unsigned n;		/* Converter Resolution */
	unsigned cs;		/* Control Bits per Sample */
	unsigned s;		/* Samples per Converter per Frame Cycle */
	unsigned nd;		/* Total Bits per Sample */
	unsigned hd;		/* High Density Format */
	unsigned fchk;		/* Checksum */
	unsigned cf;		/* Control Words per Frame Cycle per Link */
	unsigned adjcnt;	/* ADJCNT Adjustment step count */
	unsigned phyadj;	/* PHYADJ Adjustment request */
	unsigned adjdir;	/* ADJDIR Adjustment direction */
	unsigned jesdv;		/* JESD204 Version */
	unsigned subclassv;	/* JESD204 subclass version */

	unsigned mfcnt;
	unsigned ilacnt;
	unsigned errcnt;
	unsigned bufcnt;
	unsigned lecnt;
	unsigned long fc;
	unsigned ex;
	unsigned	 ey;
	unsigned cdr_data_width;
};

struct jesd204b_laneinfo lane_info[MAX_LANES];

void text_view_delete(void)
{
	if (buffer != NULL) {
		gtk_text_buffer_get_start_iter(buffer, &start);
		gtk_text_buffer_get_end_iter(buffer, &end);
		gtk_text_buffer_delete(buffer, &start, &end);
		gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);
	}
}

int get_interface(const char *path, const char *index) {

	FILE *fp;
	char cmd[512];
	int ret;

	/* flushes all open output streams */
	fflush(NULL);

	if (!path || !remote)
		path = "";

	ret = snprintf(cmd, 512,
		       "find %s/sys/bus/platform/devices -name *axi-jesd204b-rx%s* 2>/dev/null",
			path, index ? index : "");
	if (ret < 0)
		return -ENODEV;

	fp = popen(cmd, "r");
	if (fp == NULL) {
		fprintf(stderr, "can't execute find\n");
		return -errno;
	}

	if (fgets(jesd_interface_path, sizeof(jesd_interface_path), fp) != NULL){
		/* strip trailing new lines */
		if (jesd_interface_path[strlen(jesd_interface_path) - 1] == '\n')
			jesd_interface_path[strlen(jesd_interface_path) - 1] = '\0';

		pclose(fp);

		ret = snprintf(cmd, 512,
			       "find %s/sys/bus/platform/devices -name *axi-jesd-gt-rx%s* 2>/dev/null",
				path, index ? index : "");
		if (ret < 0)
			return -ENODEV;

		fp = popen(cmd, "r");
		if (fp == NULL) {
			fprintf(stderr, "can't execute find\n");
			return -errno;
		}

		if (fgets(gt_interface_path, sizeof(gt_interface_path), fp) != NULL){
			/* strip trailing new lines */
			if (gt_interface_path[strlen(gt_interface_path) - 1] == '\n')
				gt_interface_path[strlen(gt_interface_path) - 1] = '\0';
			new_interface = 1;
		} else {
			memcpy(gt_interface_path, jesd_interface_path, PATH_MAX);
			new_interface = 0;

		}

		return 0;

	}

	fprintf(stderr, "Failed to find suitable axi-jesd204b-rx interface\n");
	return -ENODEV;
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

static void analyse(unsigned long long *data, unsigned int width,
		    unsigned int height, FILE * gp)
{
	unsigned *data_u32 = (unsigned *)data;
	unsigned int x, y;
	int xmin, xmax;
	int ymin, ymax;

	xmin = -1;
	xmax = -1;
	y = (height + 1) / 2;
	for (x = 0; x < width; x++) {
		if (!(lpm ? data_u32[y * width + x] & 0x0000FFFF :
			data[y * width + x] & 0xFFFF0000FFFF)) {
			if (xmin == -1)
				xmin = x;
			xmax = x;
		}
	}

	ymin = -1;
	ymax = -1;
	x = (width + 1) / 2;
	for (y = 0; y < height; y++) {
		if (!(lpm ? data_u32[y * width + x] & 0x0000FFFF :
			data[y * width + x] & 0xFFFF0000FFFF)) {
			if (ymin == -1)
				ymin = y;
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
		(float)xmax / ((float)es_hsize) - (float)xmin / ((float)es_hsize) );
	fprintf(gp, "set label 'V: %d (CODES)' at -0.48,-120 front\n",
		ymax - ymin);

	print_output_sys(stdout, "   H: %.3f (UI)\n",
			 (float)xmax / ((float)es_hsize)  - (float)xmin / ((float)es_hsize));
	print_output_sys(stdout, "   V: %d (CODES)\n", ymax - ymin);
}

double calc_ber(unsigned long long smpl, unsigned prescale,
		unsigned long long width)
{
	unsigned long long err_ut0, err_ut1, cnt_ut0, cnt_ut1;
	double ber;

	if (lpm) {
		err_ut0 = smpl & 0xFFFF;
		cnt_ut0 = (smpl >> 16) & 0xFFFF;

		if ((err_ut0) == 0)
			ber = 1 / (double)((width << (1 + prescale)) * cnt_ut0);
		else
			ber = err_ut0 / (double)((width << (1 + prescale)) * cnt_ut0);

	} else {
		err_ut0 = smpl & 0xFFFF;
		err_ut1 = (smpl >> 32) & 0xFFFF;
		cnt_ut0 = (smpl >> 16) & 0xFFFF;
		cnt_ut1 = (smpl >> 48) & 0xFFFF;

		if ((err_ut0 + err_ut1) == 0)
			ber =
			1 / (double)((width << (1 + prescale)) *
					(cnt_ut0 + cnt_ut1));
		else
			ber = (err_ut0 * cnt_ut1 + err_ut1 * cnt_ut0) /
			(double)(2 * (width << (1 + prescale)) * cnt_ut0 * cnt_ut1);
	}

	return ber;
}

unsigned long long get_lane_rate(unsigned lane)
{
	unsigned s = lane_info[lane].hd ? 1 : lane_info[lane].s;

	return (unsigned long long)(lane_info[lane].m * s *
				    lane_info[lane].nd * 10) *
	    lane_info[lane].fc / (8 * lane_info[lane].l);
}

int plot(char *file, unsigned lane, unsigned p, char *file_png)
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
		return -1;
	}

	cnt = es_hsize * es_vsize;	/* X,Y */

	buf = malloc(cnt * (lpm ? 4 : 8));
	buf_lpm = (unsigned *) buf;

	if (buf == NULL)
		exit(EXIT_FAILURE);

	if (file_png == NULL) {
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
			 lane,
			 (double)get_lane_rate(lane) / 1000000000, lpm ? "LPM" : "DFE",
			 calc_ber(0xFFFF0000FFFF0000, p, cdr_data_width));

	fprintf(gp, "set label 'Xilinx 2D Statistical Eye Scan' at graph 0.0,1.2 left front\n");

	fprintf(gp, "set grid xtics ytics front lc rgb 'grey'\n");
	fprintf(gp, "set cblabel 'BER 10E'\n");
	fprintf(gp, "set cntrparam levels incremental -1,-1,%i\n",
		(int)log10(calc_ber(0xFFFF0000FFFF0000, p, cdr_data_width)));
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
	ret = fread(buf, lpm ? 4 : 8, cnt, pFile);
	fclose(pFile);

	if (ret != cnt) {
		print_output_sys(stderr, "%s:%d: read failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	analyse(buf, es_hsize, es_vsize, gp);

	fprintf(gp, "splot '-' using 2:1:(log10($3)) with pm3d title ' '\n");

	fflush(gp);

	for (i = 0; i < cnt; i++) {
		if (i % es_hsize == 0)
			fprintf(gp, "\n");
		fprintf(gp, "%f %f %e\n",
			((float)(i / es_hsize) - (es_vsize / 2)),
			((float)(i % es_hsize) - (es_hsize / 2)) / (es_hsize - 1),
			calc_ber(lpm ? buf_lpm[i] : buf[i], p, cdr_data_width));
	}

	fprintf(gp, "e\n");
	fflush(gp);

	if (buf)
		free(buf);
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

int get_eye_data(char *filename, char *basedir, char *filename_out)
{
	FILE *sysfsfp, *pFile;
	char temp[PATH_MAX];
	unsigned long long *buf;
	int ret = 0;
	unsigned cnt = es_hsize * es_vsize;	/* X,Y */

	buf = malloc(cnt * (lpm ? 4 : 8));
	if (buf == NULL)
		return -ENOMEM;

	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		free(buf);
		return -errno;
	}

	ret = fread(buf, lpm ? 4 : 8, cnt, sysfsfp);

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

	ret = fwrite(buf, lpm ? 4 : 8, cnt, pFile);
	fclose(pFile);
	free(buf);

	if (ret != cnt) {
		print_output_sys(stderr, "%s:%d: write failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	return 0;
}

int get_eye(unsigned lane, unsigned prescale)
{
	char temp[64];
	int ret;

	if (!work_run)
		return 0;

	sprintf(temp, "%d", prescale);
	write_sysfs(JESD204B_PRESCALE, JESD204B_GT_DEV_PATH, temp);

	sprintf(temp, "%d", lane);
	write_sysfs(JESD204B_LANE_ENABLE, JESD204B_GT_DEV_PATH, temp);

	sprintf(temp, "lane%d_p%d.eye", lane, prescale);
	ret = get_eye_data(JESD204B_EYE_DATA, JESD204B_GT_DEV_PATH, temp);
	if (ret)
		return ret;

	sprintf(temp, "Lane %d : %.2e",
		lane, calc_ber(0xFFFF0000FFFF0000, prescale, cdr_data_width));

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

int read_laneinfo(char *basedir, unsigned lane)
{
	FILE *pFile;
	char temp[PATH_MAX];
	int ret = 0;

	sprintf(temp, "%s/lane%d_info", basedir, lane);
	pFile = fopen(temp, "r");

	if (pFile == NULL) {
		return -errno;
	}

	ret = fscanf(pFile,
		     "DID: %d, BID: %d, LID: %d, L: %d, SCR: %d, F: %d\n",
		     &lane_info[lane].did,
		     &lane_info[lane].bid,
		     &lane_info[lane].lid,
		     &lane_info[lane].l,
		     &lane_info[lane].scr, &lane_info[lane].f);

	if (ret <= 0)
		return -errno;

	ret += fscanf(pFile,
		      "K: %d, M: %d, N: %d, CS: %d, S: %d, N': %d, HD: %d\n",
		      &lane_info[lane].k,
		      &lane_info[lane].m,
		      &lane_info[lane].n,
		      &lane_info[lane].cs,
		      &lane_info[lane].s,
		      &lane_info[lane].nd, &lane_info[lane].hd);

	ret += fscanf(pFile, "FCHK: 0x%X, CF: %d\n",
		      &lane_info[lane].fchk, &lane_info[lane].cf);

	ret += fscanf(pFile,
		      "ADJCNT: %d, PHYADJ: %d, ADJDIR: %d, JESDV: %d, SUBCLASS: %d\n",
		      &lane_info[lane].adjcnt,
		      &lane_info[lane].phyadj,
		      &lane_info[lane].adjdir,
		      &lane_info[lane].jesdv, &lane_info[lane].subclassv);

	ret += fscanf(pFile, "MFCNT : 0x%X\n", &lane_info[lane].mfcnt);
	ret += fscanf(pFile, "ILACNT: 0x%X\n", &lane_info[lane].ilacnt);
	ret += fscanf(pFile, "ERRCNT: 0x%X\n", &lane_info[lane].errcnt);
	ret += fscanf(pFile, "BUFCNT: 0x%X\n", &lane_info[lane].bufcnt);
	if (new_interface)
		ret += fscanf(pFile, "LECNT: 0x%X\n", &lane_info[lane].lecnt);

	ret += fscanf(pFile, "FC: %lu\n", &lane_info[lane].fc);

	if (!new_interface) {
		ret += fscanf(pFile, "x%d,y%d CDRDW: %d\n", &lane_info[lane].ex, &lane_info[lane].ey,
			&lane_info[lane].cdr_data_width);

		fclose(pFile);
	} else {
		fclose(pFile);
		sprintf(temp, "%s/info", JESD204B_GT_DEV_PATH);
		pFile = fopen(temp, "r");
		if (pFile == NULL) {
			return -errno;
		}

		lpm = 0;
		ret += fscanf(pFile, "x%d,y%d CDRDW: %d LPM: %d\n", &lane_info[lane].ex, &lane_info[lane].ey,
			&lane_info[lane].cdr_data_width, &lpm);

		fclose(pFile);
	}


	return ret;
}

void *worker(void *args)
{
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

	for (p = pmin; p <= pmax; p++) {
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressbar1),
			(float)(i++) / ((pmax - pmin) ? (pmax - pmin) : 1));

		for (l = 0; l < MAX_LANES; l++)
			if (lane_en & (1 << l))
				get_eye(l, p);
	}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressbar1), 1.0);
	return 0;
}

void save_plot_pressed_cb(GtkButton * button, gpointer user_data)
{
	GtkWidget *dialog;
	char temp[PATH_MAX];
	unsigned lane, prescale;
	gchar *item;
	double tmp, tmp2, places;
	unsigned int i;

	item =
	    gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT
					       (finished_eyes));
	if (item == NULL)
		return;

	sscanf(item, "Lane %d : %lf", &lane, &tmp);
	for (i = 0; i <= MAX_PRESCALE; i++) {
		tmp2 = calc_ber(0xFFFF0000FFFF0000, i, cdr_data_width);
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
		plot(item, lane, prescale, filename);
		g_free(filename);

	}
	free(item);
	gtk_widget_destroy(dialog);

}

void show_pressed_cb(GtkButton * button, gpointer user_data)
{
	unsigned lane, prescale=0;
	double tmp, tmp2, places;
	unsigned int i;

	gchar *item =
	    gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT
					       (finished_eyes));
	if (item == NULL)
		return;

	text_view_delete();

	sscanf(item, "Lane %d : %lf", &lane, &tmp);
	for (i = 0; i <= MAX_PRESCALE; i++) {
		tmp2 = calc_ber(0xFFFF0000FFFF0000, i, cdr_data_width);
		places = pow(10.0, round(abs(log10(tmp2))));
		tmp2 = (round(tmp2 * places * 1000.0))/(places * 1000.0);
		if (tmp2 == tmp) {
			prescale = i;
			break;
		}
	}

	sprintf(item, "lane%d_p%d.eye", lane, prescale);

	print_output_sys(stdout, "LANE%d P(%d) @ %.2f Gbps\n", lane, prescale,
			 (double)get_lane_rate(lane) / 1000000000);
	print_output_sys(stdout, "Eye Center:\n  ERR: 0 BER: %.3e\n",
			 calc_ber(0xFFFF0000FFFF0000, prescale, cdr_data_width));

	plot(item, lane, prescale, NULL);
	free(item);
}

void start_pressed_cb(GtkButton * button, gpointer user_data)
{
	work_run = 1;
	gtk_list_store_clear(GTK_LIST_STORE
			     (gtk_combo_box_get_model
			      (GTK_COMBO_BOX(finished_eyes))));
	is_first = 0;
	pthread_create(&work, NULL, worker, NULL);
}

void terminate_pressed_cb(GtkButton * button, gpointer user_data)
{
	work_run = 0;
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

static GtkTreeModel *create_and_fill_model(unsigned active_lanes)
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
		JESD204_TREE_STORE_NEW_ROW_VAL("MFCNT", lane_info[lane].mfcnt);
		JESD204_TREE_STORE_NEW_ROW_VAL("ILACNT",
					       lane_info[lane].ilacnt);
		JESD204_TREE_STORE_NEW_ROW_VAL("ERRCNT",
					       lane_info[lane].errcnt);
		JESD204_TREE_STORE_NEW_ROW_VAL("BUFCNT",
					       lane_info[lane].bufcnt);
	}

	return GTK_TREE_MODEL(treestore);
}

static GtkWidget *create_view_and_model(unsigned active_lanes)
{
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	GtkWidget *view;
	GtkTreeModel *model;

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

	model = create_and_fill_model(active_lanes);
	gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
	g_object_unref(model);

	return view;
}

int main(int argc, char *argv[])
{
	GtkWidget *box2;
	GtkWidget *box3;
	GtkWidget *view;
	GtkImage *logo;
	struct stat buf;
	int i, ret, c, cnt = 0;
	char temp[128];
	char *path = NULL;
	char *dev = NULL;
	opterr = 0;

	while ((c = getopt (argc, argv, "d:p:")) != -1)
		switch (c)
		{
		case 'd':
			dev = optarg;
			break;
		case 'p':
			path = optarg;
			remote = 1;
			break;
		case '?':
			if (optopt == 'd'|| optopt == 'p')
			fprintf (stderr, "Option -%c requires an argument.\n", optopt);
			else if (isprint (optopt))
			fprintf (stderr, "Unknown option `-%c'.\n%s [-p PATH] [-d DEVICEINDEX]\n",
				 optopt, argv[0]);
			else
			fprintf (stderr,
				"Unknown option character `\\x%x'.\n",
				optopt);
			return 1;
		default:
			abort ();
		}


	if (get_interface(path, dev))
		return EXIT_FAILURE;

	setlocale(LC_NUMERIC, "C");

	printf("Found %s\n", jesd_interface_path);
	printf("Found %s\n", gt_interface_path);

	/* init threads */
	gdk_threads_init();
	gdk_threads_enter();

	gtk_init(&argc, &argv);

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "./jesd.glade", NULL))
		gtk_builder_add_from_file(builder, "/usr/local/share/jesd/jesd.glade", NULL);

	main_window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

	lane[0] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton1"));
	lane[1] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton2"));
	lane[2] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton3"));
	lane[3] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton4"));
	lane[4] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton5"));
	lane[5] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton6"));
	lane[6] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton7"));
	lane[7] = GTK_WIDGET(gtk_builder_get_object(builder, "checkbutton8"));

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

	finished_eyes =
	    GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext1"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(finished_eyes), 0);

	min_ber = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext2"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(min_ber), 0);

	max_ber = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext3"));
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(max_ber), 0);

	gtk_builder_connect_signals(builder, NULL);

	g_signal_connect(G_OBJECT(main_window), "destroy",
			 G_CALLBACK(gtk_main_quit), NULL);

	if (!stat(JESD204B_DEV_PATH, &buf)) {
		for (i = 0; i < MAX_LANES; i++) {
			ret = read_laneinfo(JESD204B_DEV_PATH, i);
			if (ret < 0) {
				/* No such file or directory */
				if (ret == -ENOENT)
					printf("no such lane %i\n", i);
				/* No child processes */
				if (ret == -ECHILD)
					printf("not root %i, when looking for lane %i\n", ret, i);

				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lane[i]), FALSE);
				gtk_widget_hide(lane[i]);
			} else {
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lane[i]), TRUE);
				gtk_widget_show(lane[i]);
				cnt++;
			}

		}
	} else {
		print_output_sys(stderr, "Failed to find JESD device: %s\n",
				JESD204B_GT_DEV_PATH);
		return 0;
	}

	if (cnt == 0) {
		fprintf(stderr, "Failed to open %s/lane0_info : NOT ROOT?\n", JESD204B_DEV_PATH);
		return EXIT_FAILURE;
	}

	es_hsize = lane_info[0].ex;
	es_vsize = lane_info[0].ey;
	cdr_data_width = lane_info[0].cdr_data_width;

	if (!es_hsize || !es_vsize || !cdr_data_width) {
		fprintf(stderr, "Compatibility issue? possible need to update kernel\nERROR at Line %d\n", __LINE__);
		return EXIT_FAILURE;
	}

	for (i = 0; i <= MAX_PRESCALE; i++) {
		sprintf(temp, "%.2e",
			calc_ber(0xFFFF0000FFFF0000, i, cdr_data_width));
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(min_ber),
					       (const gchar *)temp);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(max_ber),
					       (const gchar *)temp);
		if (i == 0) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(min_ber), 0);
			gtk_combo_box_set_active(GTK_COMBO_BOX(max_ber), 0);

		}
	}

	box3 = GTK_WIDGET(gtk_builder_get_object(builder, "jesd_info"));
	view = create_view_and_model(cnt);
	gtk_container_add(GTK_CONTAINER(box3), view);

	logo = GTK_IMAGE(gtk_builder_get_object(builder, "logo"));

	if (!stat("./icons/ADIlogo.png", &buf)) {
		g_object_set(logo, "file", "./icons/ADIlogo.png", NULL);
	} else {
		g_object_set(logo, "file", "/usr/local/share/jesd/ADIlogo.png", NULL);
	}

	gtk_widget_show_all(main_window);

	/* enter the GTK main loop */
	gtk_main();
	gdk_threads_leave();

	return 0;
}
