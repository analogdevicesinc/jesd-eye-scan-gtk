/***************************************************************************//**
*   @file   jesd_common.h
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

#ifndef JESD_COMMON_H_
#define JESD_COMMON_H_

#define JESD204B_LANE_ENABLE	"enable"
#define JESD204B_PRESCALE	"prescale"
#define JESD204B_EYE_DATA	"eye_data"

#define MAX_LANES		32
#define MAX_DEVICES		8
#define MAX_PRESCALE		31
#define MAX_SYSFS_STRING_SIZE	32

#define PPM(x)			((x) / 1000000.0)
#define CLOCK_ACCURACY		200

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

#define        JESD204_ENCODER_8B10B   0
#define        JESD204_ENCODER_64B66B  1

#define JESD204_RX_DRIVER_NAME	"axi-jesd204-rx"
#define JESD204_TX_DRIVER_NAME	"axi-jesd204-tx"
#define XCVR_DRIVER_NAME	"axi_adxcvr_drv"

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

	unsigned long fc;

	unsigned lane_errors;
	unsigned lane_latency_multiframes;
	unsigned lane_latency_octets;

	char cgs_state[MAX_SYSFS_STRING_SIZE];
	char init_frame_sync[MAX_SYSFS_STRING_SIZE];
	char init_lane_align_seq[MAX_SYSFS_STRING_SIZE];
	char ext_multiblock_align_state[MAX_SYSFS_STRING_SIZE];
};

struct jesd204b_xcvr_eyescan_info {

	unsigned es_hsize;
	unsigned es_vsize;
	unsigned long long cdr_data_width;
	unsigned num_lanes;
	unsigned lpm;
	unsigned long lane_rate;
	char gt_interface_path[PATH_MAX];
};

struct jesd204b_jesd204_status {

	char link_state[MAX_SYSFS_STRING_SIZE];
	char measured_link_clock[MAX_SYSFS_STRING_SIZE];
	char reported_link_clock[MAX_SYSFS_STRING_SIZE];
	char lane_rate[MAX_SYSFS_STRING_SIZE];
	char lane_rate_div[MAX_SYSFS_STRING_SIZE];
	char lmfc_rate[MAX_SYSFS_STRING_SIZE];
	char sync_state[MAX_SYSFS_STRING_SIZE];
	char link_status[MAX_SYSFS_STRING_SIZE];
	char sysref_captured[MAX_SYSFS_STRING_SIZE];
	char sysref_alignment_error[MAX_SYSFS_STRING_SIZE];
	char external_reset[MAX_SYSFS_STRING_SIZE];
};

char *get_full_device_path(const char *basedir, const char *device);
int jesd_find_devices(const char *basedir, const char *drvname,
		      char devices[MAX_DEVICES][PATH_MAX], int start);
int read_laneinfo(const char *basedir, unsigned lane,
		  struct jesd204b_laneinfo *info);
int read_all_laneinfo(const char *path,
		      struct jesd204b_laneinfo lane_info[MAX_LANES]);
int read_jesd204_status(const char *basedir,
			struct jesd204b_jesd204_status *info);
int read_encoding(const char *basedir);

#endif
