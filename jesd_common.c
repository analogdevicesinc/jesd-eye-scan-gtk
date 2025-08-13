/***************************************************************************//**
*   @file   jesd_common.c
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
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "jesd_common.h"

char *get_full_device_path(const char *basedir, const char *device)
{
	char *path = malloc(PATH_MAX);
	if (!path)
		return NULL;

	snprintf(path, PATH_MAX, "%s/%s", basedir, device);

	return path;
}

int jesd_find_devices(const char *basedir, const char *driver, const char *file_exists,
		      char devices[MAX_DEVICES][PATH_MAX], int start)
{
	struct dirent *de;
	struct stat sfile, efile;
	char path[PATH_MAX];
	char stat_path[PATH_MAX];
	DIR *dr;
	int num = start, use = 1;

	snprintf(path, sizeof(path), "%s/%s", basedir, driver);
	dr = opendir(path);
	if (dr == NULL) {
		fprintf(stderr, "Could not open current directory\n");
		return 0;
	}

	while ((de = readdir(dr)) != NULL) {
		int ret_snprintf;

		ret_snprintf = snprintf(stat_path, sizeof(stat_path), "%s/%s", path, de->d_name);
		if (ret_snprintf >= (int)sizeof(stat_path))
			continue; /* Path too long, skip */

		if (lstat(stat_path, &sfile) == -1)
			continue;

		if (file_exists && S_ISLNK(sfile.st_mode)) {
			ret_snprintf = snprintf(stat_path, sizeof(stat_path), "%s/%s/%s", path, de->d_name, file_exists);
			if (ret_snprintf >= (int)sizeof(stat_path))
				continue; /* Path too long, skip */

			if (stat(stat_path, &efile) == 0)
				use = 1;
			else
				use = 0;
		}

		if (S_ISLNK(sfile.st_mode) && use && num < MAX_DEVICES) {
			snprintf((char *)&devices[num], PATH_MAX, "%s/%s", driver, de->d_name);
			num++;
		}
	}

	closedir(dr);

	return num;
}

int read_encoding(const char *basedir)
{
	char temp[PATH_MAX];
	char encoder[MAX_SYSFS_STRING_SIZE];
	FILE *f;
	int ret;

	memset(encoder, 0, sizeof(encoder));

	snprintf(temp, sizeof(temp), "%s/encoder", basedir);
	f = fopen(temp, "r");
	/*
	 * If the file is not there, default to 8b10b. It might be an older
	 * kernel and, most likely, only supports jesd204b
	 */
	if (!f && errno == ENOENT)
		return JESD204_ENCODER_8B10B;
	else if (!f)
		return -errno;

	ret = fread(encoder, sizeof(encoder) - 1, 1, f);
	if (ret != 1) {
		if (!feof(f)) {
			fclose(f);
			return JESD204_ENCODER_8B10B; /* Default to 8b10b on read error */
		}
	}

	/* Ensure null termination */
	encoder[sizeof(encoder) - 1] = '\0';

	/* Remove trailing newline if present */
	char *newline = strchr(encoder, '\n');
	if (newline)
		*newline = '\0';

	if (!strcmp(encoder, "8b10b"))
		ret = JESD204_ENCODER_8B10B;
	else
		ret = JESD204_ENCODER_64B66B;

	fclose(f);

	return ret;
}

int read_laneinfo(const char *basedir, unsigned lane,
		  struct jesd204b_laneinfo *info)
{
	FILE *pFile;
	char temp[PATH_MAX];
	int ret = 0;
	int encoder = read_encoding(basedir);

	if (encoder < 0)
		return encoder;

	memset(info, 0, sizeof(*info));

	snprintf(temp, sizeof(temp), "%s/lane%d_info", basedir, lane);

	pFile = fopen(temp, "r");

	if (pFile == NULL)
		return -errno;

	ret = fscanf(pFile, "Errors: %u\n", &info->lane_errors);
	if (encoder == JESD204_ENCODER_64B66B) {


		ret += fscanf(pFile, "State of Extended multiblock alignment:%s\n",
			      (char *)&info->ext_multiblock_align_state);
		/* Optional field - ignore if not present */
		int latency_ret = fscanf(pFile, "Lane Latency: %u (min/max %u/%u)\n",
		       &info->lane_latency_octets,
		       &info->lane_latency_min, &info->lane_latency_max);
		(void)latency_ret; /* Suppress unused variable warning */

		goto close_f;
	};
	ret += fscanf(pFile, "CGS state: %s\n", (char *)&info->cgs_state);
	ret += fscanf(pFile, "Initial Frame Synchronization: %s\n",
		      (char *)&info->init_frame_sync);
	ret += fscanf(pFile, "Lane Latency: %d Multi-frames and %d Octets\n",
		      &info->lane_latency_multiframes, &info->lane_latency_octets);
	ret += fscanf(pFile, "Initial Lane Alignment Sequence: %s\n",
		      (char *)&info->init_lane_align_seq);

	ret += fscanf(pFile,
		      "DID: %d, BID: %d, LID: %d, L: %d, SCR: %d, F: %d\n",
		      &info->did,
		      &info->bid,
		      &info->lid,
		      &info->l,
		      &info->scr, &info->f);

	if (ret <= 0) {
		int saved_errno = errno;
		fclose(pFile);
		return -saved_errno;
	}

	ret += fscanf(pFile,
		      "K: %d, M: %d, N: %d, CS: %d, N': %d, S: %d, HD: %d\n",
		      &info->k,
		      &info->m,
		      &info->n,
		      &info->cs,
		      &info->nd,
		      &info->s, &info->hd);

	ret += fscanf(pFile, "FCHK: 0x%X, CF: %d\n",
		      &info->fchk, &info->cf);

	ret += fscanf(pFile,
		      "ADJCNT: %d, PHADJ: %d, ADJDIR: %d, JESDV: %d, SUBCLASS: %d\n",
		      &info->adjcnt,
		      &info->phyadj,
		      &info->adjdir,
		      &info->jesdv, &info->subclassv);

	ret += fscanf(pFile, "FC: %lu\n", &info->fc);
close_f:
	fclose(pFile);

	return ret;
}

int read_all_laneinfo(const char *path, struct jesd204b_laneinfo lane_info[MAX_LANES])
{
	struct stat buf;
	int i, ret, cnt = 0;
	fprintf(stderr, "Failed to find JESD device: %s\n", path);
	if (!stat(path, &buf)) {
		for (i = 0; i < MAX_LANES; i++) {
			ret = read_laneinfo(path, i, &lane_info[i]);

			if (ret < 0) {
				/* No child processes */
				if (ret == -ECHILD)
					fprintf(stderr, "not root %i, when looking for lane %i\n", ret, i);

				return cnt;

			} else
				cnt++;
		}
	} else {
		fprintf(stderr, "Failed to find JESD device: %s\n", path);
		return 0;
	}

	return cnt;
}

void set_not_availabe(char *str)
{
	str[0] = 'N';
	str[1] = '/';
	str[2] = 'A';
	str[3] = 0;
}

int read_jesd204_status(const char *basedir,
			struct jesd204b_jesd204_status *info)
{
	FILE *pFile;
	char temp[PATH_MAX];
	long pos;
	int ret = 0;
	int encoder = read_encoding(basedir);

	if (encoder < 0)
		return encoder;

	snprintf(temp, sizeof(temp), "%s/status", basedir);

	memset(info, 0, sizeof(*info));

	pFile = fopen(temp, "r");

	if (pFile == NULL) {
		fprintf(stderr, "Failed to read JESD204 device file: %s\n",
			temp);
		return -errno;
	}

	ret = fscanf(pFile, "Link is %s\n", (char *)&info->link_state);
	ret = fscanf(pFile, "Measured Link Clock: %s MHz\n",
		     (char *)&info->measured_link_clock);
	ret = fscanf(pFile, "Reported Link Clock: %s MHz\n",
		     (char *)&info->reported_link_clock);

	pos = ftell(pFile);
	ret = fscanf(pFile, "Measured Device Clock: %s MHz\n",
		     (char *)&info->measured_device_clock);

	if (ret == 1) {
		ret = fscanf(pFile, "Reported Device Clock: %s MHz\n",
			     (char *)&info->reported_device_clock);
		ret = fscanf(pFile, "Desired Device Clock: %s MHz\n",
			     (char *)&info->desired_device_clock);
	} else {
		set_not_availabe(info->measured_device_clock);
		set_not_availabe(info->reported_device_clock);
		set_not_availabe(info->desired_device_clock);
		fseek(pFile, pos, SEEK_SET);
	}

	pos = ftell(pFile);
	ret = fscanf(pFile, "Lane rate: %s MHz\n", (char *)&info->lane_rate);

	if (ret != 1) {
		fseek(pFile, pos, SEEK_SET);
		ret = fscanf(pFile, "External reset is %s\n", (char *)&info->external_reset);
		fclose(pFile);

		return ret;
	}

	if (encoder == JESD204_ENCODER_8B10B) {
		ret = fscanf(pFile, "Lane rate / 40: %s MHz\n", (char *)&info->lane_rate_div);
		ret = fscanf(pFile, "LMFC rate: %s MHz\n", (char *)&info->lmfc_rate);

		/* Only on TX */
		pos = ftell(pFile);
		ret = fscanf(pFile, "SYNC~: %s\n", (char *)&info->sync_state);

		if (ret != 1)
			fseek(pFile, pos, SEEK_SET);
	} else {
		ret = fscanf(pFile, "Lane rate / 66: %s MHz\n", (char *)&info->lane_rate_div);
		ret = fscanf(pFile, "LEMC rate: %s MHz\n", (char *)&info->lmfc_rate);
	}
	ret = fscanf(pFile, "Link status: %s\n", (char *)&info->link_status);
	ret = fscanf(pFile, "SYSREF captured: %s\n", (char *)&info->sysref_captured);
	ret = fscanf(pFile, "SYSREF alignment error: %s\n",
		     (char *)&info->sysref_alignment_error);

	fclose(pFile);

	return ret;
}

/* Global IIO context for unified API */
struct jesd_iio_context *g_jesd_iio_ctx = NULL;

/* =================================================================== */
/* libiio implementation - always compiled, but may be stubs */
/* =================================================================== */

#ifdef USE_LIBIIO
/* Real libiio implementation when USE_LIBIIO is defined */


static inline const char *get_label_or_name_or_id(const struct iio_device *dev)
{
	const char *label, *name;

	label = iio_device_get_label(dev);
	if (label)
		return label;

	name = iio_device_get_name(dev);
	if (name)
		return name;

	return iio_device_get_id(dev);
}

struct jesd_iio_context *jesd_iio_create_context(const char *uri)
{
	struct jesd_iio_context *jctx;
	struct iio_context *ctx;
	unsigned int i, nb_devices;

	jctx = calloc(1, sizeof(*jctx));
	if (!jctx) {
		return NULL;
	}

	if (uri) {
		ctx = iio_create_context_from_uri(uri);
		jctx->uri = strdup(uri);
		if (!jctx->uri) {
			free(jctx);
			return NULL;
		}
	} else {
		ctx = iio_create_default_context();
	}

	if (!ctx) {
		free(jctx->uri);
		free(jctx);
		return NULL;
	}

	jctx->ctx = ctx;
	/* Find JESD204 devices - any device with "axi-jesd204-" in the name */
	nb_devices = iio_context_get_devices_count(ctx);
	jctx->num_jesd_devices = 0;
	jctx->num_xcvr_devices = 0;

	for (i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = get_label_or_name_or_id(dev);

		if (!name)
			continue;

		/* Add any device with "axi-jesd204-" in the name */
		if (strstr(name, "axi-jesd204-") && jctx->num_jesd_devices < MAX_DEVICES) {
			jctx->jesd_devices[jctx->num_jesd_devices] = dev;
			jctx->num_jesd_devices++;
		}
		/* Add any transceiver device with "axi-adxcvr" or "axi_adxcvr" in the name */
		else if ((strstr(name, "axi-adxcvr") || strstr(name, "axi_adxcvr")) &&
		         jctx->num_xcvr_devices < MAX_DEVICES) {
			jctx->xcvr_devices[jctx->num_xcvr_devices] = dev;
			jctx->num_xcvr_devices++;
		}
	}

	return jctx;
}

void jesd_iio_destroy_context(struct jesd_iio_context *jctx)
{
	if (!jctx)
		return;

	if (jctx->ctx)
		iio_context_destroy(jctx->ctx);

	free(jctx->uri);
	free(jctx);
}

int jesd_iio_read_attr(struct iio_device *dev, const char *attr,
		       char *buf, size_t len)
{
	ssize_t ret;

	if (!dev || !attr || !buf)
		return -EINVAL;

	ret = iio_device_attr_read(dev, attr, buf, len);
	if (ret < 0)
		return ret;

	/* Remove trailing newline if present */
	if (ret > 0 && buf[ret-1] == '\n')
		buf[ret-1] = '\0';

	return 0;
}

int jesd_iio_write_attr(struct iio_device *dev, const char *attr,
			const char *value)
{
	if (!dev || !attr || !value)
		return -EINVAL;

	return iio_device_attr_write(dev, attr, value);
}

int jesd_iio_device_attr_read_longlong(struct iio_device *dev, const char *attr,
					  long long int *value)
{
	return iio_device_attr_read_longlong(dev, attr, value);
}

int jesd_iio_device_attr_read(struct iio_device *dev, const char *attr,
			      char *buf, size_t len)
{
	if (!dev || !attr || !buf)
		return -EINVAL;

	return iio_device_attr_read(dev, attr, buf, len);
}

int jesd_iio_find_devices(struct jesd_iio_context *jctx,
			  char devices[MAX_DEVICES][PATH_MAX])
{
	int i;

	if (!jctx || !jctx->ctx)
		return 0;

	for (i = 0; i < jctx->num_jesd_devices; i++) {
		const char *name = get_label_or_name_or_id(jctx->jesd_devices[i]);
		snprintf(devices[i], PATH_MAX, "iio:%s", name ?: "jesd-device");
	}

	return jctx->num_jesd_devices;
}

int jesd_iio_find_xcvr_devices(struct jesd_iio_context *jctx,
			       char devices[MAX_DEVICES][PATH_MAX])
{
	int i;

	if (!jctx || !jctx->ctx)
		return 0;

	for (i = 0; i < jctx->num_xcvr_devices; i++) {
		const char *name = get_label_or_name_or_id(jctx->xcvr_devices[i]);
		snprintf(devices[i], PATH_MAX, "iio:%s", name ?: "xcvr-device");
	}

	return jctx->num_xcvr_devices;
}

int jesd_iio_read_encoding(struct iio_device *dev)
{
	char buf[MAX_SYSFS_STRING_SIZE];
	int ret;

	ret = jesd_iio_read_attr(dev, "encoder", buf, sizeof(buf));
	if (ret < 0)
		return JESD204_ENCODER_8B10B; /* Default to 8b10b */

	if (!strcmp(buf, "8b10b"))
		return JESD204_ENCODER_8B10B;
	else
		return JESD204_ENCODER_64B66B;
}

static void parse_lane_info_line(const char *line, const char *key, unsigned *value)
{
	char *pos = strstr(line, key);
	if (pos) {
		sscanf(pos + strlen(key), "%u", value);
	}
}

static void parse_lane_info_string(const char *line, const char *key, char *value, size_t len)
{
	char *pos = strstr(line, key);

	if (pos) {
		sscanf(pos + strlen(key), "%s\n", value);
	}
}

int jesd_iio_read_laneinfo(struct iio_device *dev, unsigned lane,
			   struct jesd204b_laneinfo *info)
{
	char attr_name[32];
	char buf[1024];
	int ret;
	int encoder;

	if (!dev || !info)
		return -EINVAL;

	memset(info, 0, sizeof(*info));

	encoder = jesd_iio_read_encoding(dev);

	snprintf(attr_name, sizeof(attr_name), "lane%u_info", lane);
	ret = jesd_iio_read_attr(dev, attr_name, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/* Parse based on encoder type */
	if (encoder == JESD204_ENCODER_8B10B) {
		/* Parse 8b10b format */
		parse_lane_info_line(buf, "DID:", &info->did);
		parse_lane_info_line(buf, "BID:", &info->bid);
		parse_lane_info_line(buf, "LID:", &info->lid);
		parse_lane_info_line(buf, "L:", &info->l);
		parse_lane_info_line(buf, "SCR:", &info->scr);
		parse_lane_info_line(buf, "F:", &info->f);
		parse_lane_info_line(buf, "K:", &info->k);
		parse_lane_info_line(buf, "M:", &info->m);
		parse_lane_info_line(buf, "N:", &info->n);
		parse_lane_info_line(buf, "CS:", &info->cs);
		parse_lane_info_line(buf, "N':", &info->nd);
		parse_lane_info_line(buf, "S:", &info->s);
		parse_lane_info_line(buf, "HD:", &info->hd);
		parse_lane_info_line(buf, "FCHK:", &info->fchk);
		parse_lane_info_line(buf, "CF:", &info->cf);
		parse_lane_info_line(buf, "ADJCNT:", &info->adjcnt);
		parse_lane_info_line(buf, "PHYADJ:", &info->phyadj);
		parse_lane_info_line(buf, "ADJDIR:", &info->adjdir);
		parse_lane_info_line(buf, "JESDV:", &info->jesdv);
		parse_lane_info_line(buf, "SUBCLASSV:", &info->subclassv);
		{
			unsigned fc_tmp = 0;
			parse_lane_info_line(buf, "FC:", &fc_tmp);
			info->fc = fc_tmp;
		}
		parse_lane_info_line(buf, "Errors:", &info->lane_errors);
		parse_lane_info_string(buf, "CGS state:", info->cgs_state, sizeof(info->cgs_state));
		parse_lane_info_string(buf, "Initial Frame Synchronization:",
				       info->init_frame_sync, sizeof(info->init_frame_sync));
		parse_lane_info_string(buf, "Lane Latency:", buf, sizeof(buf));
		sscanf(buf, "Lane Latency: %u %u", &info->lane_latency_multiframes,
		       &info->lane_latency_octets);
	} else {
		/* Parse 64b66b format */
		parse_lane_info_line(buf, "Errors:", &info->lane_errors);
		parse_lane_info_string(buf, "State of Extended multiblock alignment:",
				       info->ext_multiblock_align_state,
				       sizeof(info->ext_multiblock_align_state));

		char *latency_pos = strstr(buf, "Lane Latency:");
		if (latency_pos) {
			ret = sscanf(latency_pos, "Lane Latency: %u (min/max %u/%u",
			       &info->lane_latency_octets, &info->lane_latency_min,
			       &info->lane_latency_max);

		}
	}

	return 0;
}

int jesd_iio_read_all_laneinfo(struct iio_device *dev,
			       struct jesd204b_laneinfo lane_info[MAX_LANES])
{
	unsigned lane;
	int ret, lanes_found = 0;

	for (lane = 0; lane < MAX_LANES; lane++) {
		ret = jesd_iio_read_laneinfo(dev, lane, &lane_info[lane]);
		if (ret == 0)
			lanes_found++;
	}

	return lanes_found;
}

int jesd_iio_read_jesd204_status(struct iio_device *dev,
				 struct jesd204b_jesd204_status *info)
{
	char buf[2048];
	int ret;
	char *line, *saveptr;

	if (!dev || !info)
		return -EINVAL;

	memset(info, 0, sizeof(*info));

	ret = jesd_iio_read_attr(dev, "status", buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/* Parse status line by line */
	line = strtok_r(buf, "\n", &saveptr);
	while (line) {
		if (strstr(line, "Link is"))
			sscanf(line, "Link is %s", info->link_state);
		else if (strstr(line, "Measured Link Clock:"))
			sscanf(line, "Measured Link Clock: %s", info->measured_link_clock);
		else if (strstr(line, "Reported Link Clock:"))
			sscanf(line, "Reported Link Clock: %s MHz", info->reported_link_clock);
		else if (strstr(line, "Measured Device Clock:"))
			sscanf(line, "Measured Device Clock: %s MHz", info->measured_device_clock);
		else if (strstr(line, "Reported Device Clock:"))
			sscanf(line, "Reported Device Clock: %s MHz", info->reported_device_clock);
		else if (strstr(line, "Desired Device Clock:"))
			sscanf(line, "Desired Device Clock: %s MHz", info->desired_device_clock);
		else if (strstr(line, "Lane rate:"))
			sscanf(line, "Lane rate: %s MHz", info->lane_rate);
		else if (strstr(line, "Lane rate / 40:"))
			sscanf(line, "Lane rate / 40: %s MHz", info->lane_rate_div);
		else if (strstr(line, "Lane rate / 66:"))
			sscanf(line, "Lane rate / 66: %s MHz", info->lane_rate_div);
		else if (strstr(line, "LMFC rate:"))
			sscanf(line, "LMFC rate: %s MHz", info->lmfc_rate);
		else if (strstr(line, "LEMC rate:"))
			sscanf(line, "LEMC rate: %s MHz", info->lmfc_rate);
		else if (strstr(line, "SYNC~:"))
			sscanf(line, "SYNC~: %s", info->sync_state);
		else if (strstr(line, "Link status:"))
			sscanf(line, "Link status: %s", info->link_status);
		else if (strstr(line, "SYSREF captured:"))
			sscanf(line, "SYSREF captured: %s", info->sysref_captured);
		else if (strstr(line, "SYSREF alignment error:"))
			sscanf(line, "SYSREF alignment error: %s", info->sysref_alignment_error);
		else if (strstr(line, "External reset is"))
			sscanf(line, "External reset is %s", info->external_reset);

		line = strtok_r(NULL, "\n", &saveptr);
	}

	/* Set N/A for missing device clock fields if needed */
	if (strlen(info->measured_device_clock) == 0)
		strcpy(info->measured_device_clock, "N/A");
	if (strlen(info->reported_device_clock) == 0)
		strcpy(info->reported_device_clock, "N/A");
	if (strlen(info->desired_device_clock) == 0)
		strcpy(info->desired_device_clock, "N/A");

	return 0;
}

#else
/* Stub implementations when USE_LIBIIO is not defined */

struct jesd_iio_context *jesd_iio_create_context(const char *uri)
{
	(void)uri;  /* Suppress unused parameter warning */
	return NULL;  /* Always fail when libiio not available */
}

void jesd_iio_destroy_context(struct jesd_iio_context *ctx)
{
	(void)ctx;  /* Nothing to do */
}

int jesd_iio_find_devices(struct jesd_iio_context *ctx,
			  char devices[MAX_DEVICES][PATH_MAX])
{
	(void)ctx;
	(void)devices;
	return 0;  /* No devices found */
}

int jesd_iio_find_xcvr_devices(struct jesd_iio_context *ctx,
			       char devices[MAX_DEVICES][PATH_MAX])
{
	(void)ctx;
	(void)devices;
	return 0;  /* No devices found */
}

int jesd_iio_read_encoding(struct iio_device *dev)
{
	(void)dev;
	return JESD204_ENCODER_8B10B;  /* Default fallback */
}

int jesd_iio_read_laneinfo(struct iio_device *dev, unsigned lane,
			   struct jesd204b_laneinfo *info)
{
	(void)dev;
	(void)lane;
	(void)info;
	return -ENOSYS;  /* Not implemented */
}

int jesd_iio_read_all_laneinfo(struct iio_device *dev,
			       struct jesd204b_laneinfo lane_info[MAX_LANES])
{
	(void)dev;
	(void)lane_info;
	return 0;  /* No lanes found */
}

int jesd_iio_read_jesd204_status(struct iio_device *dev,
				 struct jesd204b_jesd204_status *info)
{
	(void)dev;
	(void)info;
	return -ENOSYS;  /* Not implemented */
}

int jesd_iio_read_attr(struct iio_device *dev, const char *attr,
		       char *buf, size_t len)
{
	(void)dev;
	(void)attr;
	(void)buf;
	(void)len;
	return -ENOSYS;  /* Not implemented */
}

int jesd_iio_write_attr(struct iio_device *dev, const char *attr,
			const char *value)
{
	(void)dev;
	(void)attr;
	(void)value;
	return -ENOSYS;  /* Not implemented */
}

int jesd_iio_device_attr_read_longlong(struct iio_device *dev, const char *attr,
			 long long *value)
{
	(void)dev;
	(void)attr;
	(void)value;
	return -ENOSYS;  /* Not implemented */
}

int jesd_iio_device_attr_read(struct iio_device *dev, const char *attr,
			       char *buf, size_t len)
{
	(void)dev;
	(void)attr;
	(void)buf;
	(void)len;
	return -ENOSYS;  /* Not implemented */
}

#endif /* USE_LIBIIO */

/* =================================================================== */
/* Unified wrapper functions - always available */
/* =================================================================== */

struct iio_device *get_iio_device_from_path(const char *path_or_device)
{
#ifdef USE_LIBIIO
	int i;
	const char *device_name;

	if (!g_jesd_iio_ctx || !path_or_device || !strstr(path_or_device, "iio:"))
		return NULL;

	/* Extract the device name from "iio:device_name" */
	device_name = path_or_device + strlen("iio:");  /* Skip "iio:" prefix */

	/* Search through JESD204 devices first */
	for (i = 0; i < g_jesd_iio_ctx->num_jesd_devices; i++) {
		const char *name = get_label_or_name_or_id(g_jesd_iio_ctx->jesd_devices[i]);
		if (name && strcmp(name, device_name) == 0) {
			return g_jesd_iio_ctx->jesd_devices[i];
		}
	}


	device_name = path_or_device + strlen("iio:context/iio:");  /* Skip prefix */
	/* Check transceiver devices for eye scan functionality */
	if (strstr(path_or_device, "adxcvr")) {
		for (i = 0; i < g_jesd_iio_ctx->num_xcvr_devices; i++) {
			const char *name = get_label_or_name_or_id(g_jesd_iio_ctx->xcvr_devices[i]);
			if (name && strcmp(name, device_name) == 0) {
				return g_jesd_iio_ctx->xcvr_devices[i];
			}
		}
	}

	return NULL;
#else
	/* When libiio is not available, never return an IIO device */
	(void)path_or_device;  /* Suppress unused parameter warning */
	return NULL;
#endif
}

int jesd_read_encoding(const char *path_or_device)
{
	struct iio_device *dev = get_iio_device_from_path(path_or_device);
	if (dev) {
		return jesd_iio_read_encoding(dev);
	}
	return read_encoding(path_or_device);
}

int jesd_read_laneinfo(const char *path_or_device, unsigned lane,
		       struct jesd204b_laneinfo *info)
{
	struct iio_device *dev = get_iio_device_from_path(path_or_device);
	if (dev) {
		return jesd_iio_read_laneinfo(dev, lane, info);
	}
	return read_laneinfo(path_or_device, lane, info);
}

int jesd_read_all_laneinfo(const char *path_or_device,
			   struct jesd204b_laneinfo lane_info[MAX_LANES])
{
	struct iio_device *dev = get_iio_device_from_path(path_or_device);
	if (dev) {
		return jesd_iio_read_all_laneinfo(dev, lane_info);
	}
	return read_all_laneinfo(path_or_device, lane_info);
}

int jesd_read_jesd204_status(const char *path_or_device,
			     struct jesd204b_jesd204_status *info)
{
	struct iio_device *dev = get_iio_device_from_path(path_or_device);
	if (dev) {
		return jesd_iio_read_jesd204_status(dev, info);
	}
	return read_jesd204_status(path_or_device, info);
}

int jesd_read_attr(const char *path_or_device, const char *attr, char *buf, size_t len)
{
	struct iio_device *dev = get_iio_device_from_path(path_or_device);
	if (dev) {
		return jesd_iio_read_attr(dev, attr, buf, len);
	}
	/* For sysfs, construct the full path and read the file */
	char full_path[PATH_MAX];
	FILE *fp;
	size_t read_len;

	snprintf(full_path, sizeof(full_path), "%s/%s", path_or_device, attr);
	fp = fopen(full_path, "r");
	if (!fp)
		return -errno;

	read_len = fread(buf, 1, len - 1, fp);
	fclose(fp);

	if (read_len > 0) {
		buf[read_len] = '\0';
		/* Remove trailing newline if present */
		if (read_len > 0 && buf[read_len-1] == '\n')
			buf[read_len-1] = '\0';
		return 0;
	}

	return -EIO;
}

int jesd_write_attr(const char *path_or_device, const char *attr, const char *value)
{
	struct iio_device *dev = get_iio_device_from_path(path_or_device);
	if (dev) {
		return jesd_iio_write_attr(dev, attr, value);
	}
	/* For sysfs, construct the full path and write the file */
	char full_path[PATH_MAX];
	FILE *fp;
	int ret;

	snprintf(full_path, sizeof(full_path), "%s/%s", path_or_device, attr);
	fp = fopen(full_path, "w");
	if (!fp)
		return -errno;

	ret = fprintf(fp, "%s", value);
	fclose(fp);

	return ret < 0 ? ret : 0;
}
