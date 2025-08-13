/***************************************************************************//**
*   @file   jesd_common.h
*   @brief  JESD204 Status Information Utility
*   @author Michael Hennerich (michael.hennerich@analog.com)
********************************************************************************
* Copyright 2014-2025(c) Analog Devices, Inc. All rights reserved.
*
* An ADI specific BSD license, which can be found in the top level directory
* of this repository (LICENSE.txt), and also on-line at:
* https://github.com/analogdevicesinc/jesd-eye-scan-gtk/blob/main/LICENSE.txt
*******************************************************************************/

#ifndef JESD_COMMON_H_
#define JESD_COMMON_H_

/* Always include libiio headers when available, but gracefully degrade */
#ifdef USE_LIBIIO
#include <iio.h>
#else
/* Stub definitions when libiio is not available */
struct iio_context;
struct iio_device;
#endif

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

/* Use GLib's MAX/MIN macros if available, otherwise define our own */
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#define JESD204_ENCODER_8B10B   0
#define JESD204_ENCODER_64B66B  1

#define JESD204_RX_DRIVER_NAME	"axi-jesd204-rx"
#define JESD204_TX_DRIVER_NAME	"axi-jesd204-tx"
#define XCVR_DRIVER_NAME	"axi_adxcvr"
#define XCVR_NEW_DRIVER_NAME	"axi_adxcvr_drv"

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
	unsigned lane_latency_min; /* 204C modes only */
	unsigned lane_latency_max; /* 204C modes only */

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
	char measured_device_clock[MAX_SYSFS_STRING_SIZE];
	char reported_device_clock[MAX_SYSFS_STRING_SIZE];
	char desired_device_clock[MAX_SYSFS_STRING_SIZE];
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
int jesd_find_devices(const char *basedir, const char *driver, const char *file_exists,
		      char devices[MAX_DEVICES][PATH_MAX], int start);
int read_laneinfo(const char *basedir, unsigned lane,
		  struct jesd204b_laneinfo *info);
int read_all_laneinfo(const char *path,
		      struct jesd204b_laneinfo lane_info[MAX_LANES]);
int read_jesd204_status(const char *basedir,
			struct jesd204b_jesd204_status *info);
int read_encoding(const char *basedir);

/* libiio-based structures - always available */
struct jesd_iio_context {
	struct iio_context *ctx;
	struct iio_device *jesd_devices[MAX_DEVICES];  /* All JESD204 devices */
	int num_jesd_devices;
	struct iio_device *xcvr_devices[MAX_DEVICES];  /* All transceiver devices */
	int num_xcvr_devices;
	char *uri;  /* NULL for local context */
};

/* Unified API - always available, implemented differently based on USE_LIBIIO */
struct jesd_iio_context *jesd_iio_create_context(const char *uri);
void jesd_iio_destroy_context(struct jesd_iio_context *ctx);
int jesd_iio_find_devices(struct jesd_iio_context *ctx,
			  char devices[MAX_DEVICES][PATH_MAX]);
int jesd_iio_find_xcvr_devices(struct jesd_iio_context *ctx,
			       char devices[MAX_DEVICES][PATH_MAX]);
int jesd_iio_read_encoding(struct iio_device *dev);
int jesd_iio_read_laneinfo(struct iio_device *dev, unsigned lane,
			   struct jesd204b_laneinfo *info);
int jesd_iio_read_all_laneinfo(struct iio_device *dev,
			       struct jesd204b_laneinfo lane_info[MAX_LANES]);
int jesd_iio_read_jesd204_status(struct iio_device *dev,
				 struct jesd204b_jesd204_status *info);
int jesd_iio_read_attr(struct iio_device *dev, const char *attr,
		       char *buf, size_t len);
int jesd_iio_write_attr(struct iio_device *dev, const char *attr,
			const char *value);
int jesd_iio_device_attr_read_longlong(struct iio_device *dev,
					  const char *attr,
					  long long int *value);
int jesd_iio_device_attr_read(struct iio_device *dev, const char *attr,
			      char *buf, size_t len);
/* Unified wrapper functions that automatically choose sysfs or libiio */
int jesd_read_encoding(const char *path_or_device);
int jesd_read_laneinfo(const char *path_or_device, unsigned lane,
		       struct jesd204b_laneinfo *info);
int jesd_read_all_laneinfo(const char *path_or_device,
			   struct jesd204b_laneinfo lane_info[MAX_LANES]);
int jesd_read_jesd204_status(const char *path_or_device,
			     struct jesd204b_jesd204_status *info);
int jesd_write_attr(const char *path_or_device, const char *attr, const char *value);
int jesd_read_attr(const char *path_or_device, const char *attr, char *buf, size_t len);

struct iio_device *get_iio_device_from_path(const char *path_or_device);

/* Global context management */
extern struct jesd_iio_context *g_jesd_iio_ctx;


#endif
