/***************************************************************************//**
*   @file   jesd_common.c
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
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "jesd_common.h"

char *get_full_device_path(const char *basedir, const char *device)
{
	static char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", basedir, device);

	return path;
}

int jesd_find_devices(const char *basedir, const char *name,
		      char devices[MAX_DEVICES][PATH_MAX])
{
	struct dirent *de;
	DIR *dr = opendir(basedir);
	int num = 0;

	if (dr == NULL) {
		fprintf(stderr, "Could not open current directory");
		return 0;
	}

	while ((de = readdir(dr)) != NULL)
		if (strstr(de->d_name, name) && num < MAX_DEVICES) {
			strncpy((char *)&devices[num], de->d_name,
				sizeof(devices[num]));
			num++;
		}

	closedir(dr);

	return num;
}

int read_laneinfo(const char *basedir, unsigned lane,
		  struct jesd204b_laneinfo *info)
{
	FILE *pFile;
	char temp[PATH_MAX];
	int ret = 0;

	memset(info, 0, sizeof(*info));

	sprintf(temp, "%s/lane%d_info", basedir, lane);

	pFile = fopen(temp, "r");

	if (pFile == NULL)
		return -errno;

	ret = fscanf(pFile, "Errors: %u\n", &info->lane_errors);
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
		fclose(pFile);
		return -errno;
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

	fclose(pFile);

	return ret;
}

int read_all_laneinfo(const char *path, struct jesd204b_laneinfo lane_info[MAX_LANES])
{
	struct stat buf;
	int i, ret, cnt = 0;

	if (!stat(path, &buf)) {
		for (i = 0; i < MAX_LANES; i++) {
			ret = read_laneinfo(path, i, &lane_info[i]);

			if (ret < 0) {
				/* No child processes */
				if (ret == -ECHILD)
					fprintf(stderr, "not root %i, when looking for lane %i\n", ret, i);

				return cnt;

			} else {
				cnt++;
			}
		}
	} else {
		fprintf(stderr, "Failed to find JESD device: %s\n", path);
		return 0;
	}

	return cnt;
}


int read_jesd204_status(const char *basedir,
			struct jesd204b_jesd204_status *info)
{
	FILE *pFile;
	char temp[PATH_MAX];
	long pos;
	int ret = 0;

	sprintf(temp, "%s/status", basedir);

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
	ret = fscanf(pFile, "Lane rate: %s MHz\n", (char *)&info->lane_rate);

	if (ret != 1) {
		fseek(pFile, pos, SEEK_SET);
		ret = fscanf(pFile, "External reset is %s\n", (char *)&info->external_reset);
		fclose(pFile);

		return ret;
	}

	ret = fscanf(pFile, "Lane rate / 40: %s MHz\n", (char *)&info->lane_rate_div);
	ret = fscanf(pFile, "LMFC rate: %s MHz\n", (char *)&info->lmfc_rate);

	/* Only on TX */
	pos = ftell(pFile);
	ret = fscanf(pFile, "SYNC~: %s\n", (char *)&info->sync_state);

	if (ret != 1)
		fseek(pFile, pos, SEEK_SET);

	ret = fscanf(pFile, "Link status: %s\n", (char *)&info->link_status);
	ret = fscanf(pFile, "SYSREF captured: %s\n", (char *)&info->sysref_captured);
	ret = fscanf(pFile, "SYSREF alignment error: %s\n",
		     (char *)&info->sysref_alignment_error);

	fclose(pFile);

	return ret;
}
