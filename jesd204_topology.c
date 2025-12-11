/***************************************************************************//**
*   @file   jesd204_topology.c
*   @brief  JESD204 Topology Viewer Tool
*
*   Parses /sys/bus/jesd204 to display JESD204 device topology information
*   and optionally generates DOT files for visualization.
*
*   @author Michael Hennerich (michael.hennerich@analog.com)
********************************************************************************
* Copyright 2024-2025(c) Analog Devices, Inc. All rights reserved.
*
* An ADI specific BSD license, which can be found in the top level directory
* of this repository (LICENSE.txt), and also on-line at:
* https://github.com/analogdevicesinc/jesd-eye-scan-gtk/blob/main/LICENSE.txt
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>

#define JESD204_SYSFS_PATH	"/sys/bus/jesd204/devices"
#define MAX_PATH_LEN		PATH_MAX
#define MAX_NAME_LEN		256
#define MAX_DEVICES		64
#define MAX_LINKS		16
#define MAX_CONNECTIONS		128

/* JESD204 link parameters */
struct jesd204_link_info {
	unsigned int link_id;
	int error;
	char state[64];
	bool fsm_paused;
	bool fsm_ignore_errors;
	unsigned long long sample_rate;
	unsigned int sample_rate_div;
	bool is_transmit;
	unsigned int num_lanes;
	unsigned int num_converters;
	unsigned int octets_per_frame;
	unsigned int frames_per_multiframe;
	unsigned int num_of_multiblocks_in_emb;
	unsigned int bits_per_sample;
	unsigned int converter_resolution;
	unsigned int jesd_version;
	unsigned int jesd_encoder;
	unsigned int subclass;
	unsigned int device_id;
	unsigned int bank_id;
	bool scrambling;
	bool high_density;
	unsigned int ctrl_words_per_frame_clk;
	unsigned int ctrl_bits_per_sample;
	unsigned int samples_per_conv_frame;
};

/* Connection information (input connections) */
struct jesd204_connection {
	char from_device[MAX_NAME_LEN];  /* Device that owns this connection */
	char to_device[MAX_NAME_LEN];    /* Connected device (from "to" or "in_to") */
	unsigned int con_id;
	unsigned int topo_id;
	unsigned int link_id;
	char state[64];
	int error;
	bool is_input;  /* true for input connections, false for output */
};

/* JESD204 device information */
struct jesd204_device {
	char name[MAX_NAME_LEN];
	char sysfs_name[MAX_NAME_LEN];   /* jesd204:X name */
	char sysfs_path[MAX_PATH_LEN];
	bool is_top;
	int topology_id;
	unsigned int num_links;
	unsigned int num_retries;
	struct jesd204_link_info links[MAX_LINKS];
	unsigned int num_input_cons;
	struct jesd204_connection input_cons[MAX_CONNECTIONS];
	unsigned int num_output_cons;
	struct jesd204_connection output_cons[MAX_CONNECTIONS];
};

/* Global device list */
static struct jesd204_device devices[MAX_DEVICES];
static int num_devices = 0;

/* Program options */
static struct {
	bool verbose;
	bool generate_dot;
	bool ascii_graph;
	bool set_ignore_errors;
	bool clear_ignore_errors;
	int ignore_errors_link;  /* -1 for all links, >= 0 for specific link */
	char dot_filename[MAX_PATH_LEN];
	char sysfs_path[MAX_PATH_LEN];
} options = {
	.verbose = false,
	.generate_dot = false,
	.ascii_graph = false,
	.set_ignore_errors = false,
	.clear_ignore_errors = false,
	.ignore_errors_link = -1,
	.dot_filename = "jesd204_topology.dot",
	.sysfs_path = JESD204_SYSFS_PATH,
};

static void print_usage(const char *progname)
{
	printf("Usage: %s [OPTIONS]\n", progname);
	printf("\n");
	printf("Parse JESD204 sysfs and display topology information.\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --help              Show this help message\n");
	printf("  -v, --verbose           Enable verbose output\n");
	printf("  -g, --graph             Display ASCII topology graph\n");
	printf("  -d, --dot <file>        Generate DOT file for graphviz visualization\n");
	printf("  -p, --path <path>       Override sysfs path (default: %s)\n", JESD204_SYSFS_PATH);
	printf("  -i, --ignore-errors [link]   Set fsm_ignore_errors for link (or all if no link specified)\n");
	printf("  -I, --no-ignore-errors [link] Clear fsm_ignore_errors for link (or all if no link specified)\n");
	printf("\n");
	printf("Examples:\n");
	printf("  %s                      Display topology summary\n", progname);
	printf("  %s -d topology.dot      Generate DOT file\n", progname);
	printf("  %s -v -d output.dot     Verbose output with DOT generation\n", progname);
	printf("  %s -i                   Set ignore_errors on all links\n", progname);
	printf("  %s -i 0                 Set ignore_errors on link 0 only\n", progname);
	printf("  %s -I                   Clear ignore_errors on all links\n", progname);
	printf("\n");
	printf("To visualize the DOT file:\n");
	printf("  dot -Tpng topology.dot -o topology.png\n");
	printf("  dot -Tsvg topology.dot -o topology.svg\n");
}

static int read_sysfs_string(const char *path, char *buf, size_t len)
{
	FILE *f;
	char *newline;

	f = fopen(path, "r");
	if (!f)
		return -errno;

	if (!fgets(buf, len, f)) {
		fclose(f);
		return -EIO;
	}

	fclose(f);

	/* Remove trailing newline */
	newline = strchr(buf, '\n');
	if (newline)
		*newline = '\0';

	return 0;
}

static int read_sysfs_uint(const char *path, unsigned int *val)
{
	char buf[64];
	int ret;

	ret = read_sysfs_string(path, buf, sizeof(buf));
	if (ret)
		return ret;

	*val = strtoul(buf, NULL, 0);
	return 0;
}

static int read_sysfs_int(const char *path, int *val)
{
	char buf[64];
	int ret;

	ret = read_sysfs_string(path, buf, sizeof(buf));
	if (ret)
		return ret;

	*val = strtol(buf, NULL, 0);
	return 0;
}

static int read_sysfs_bool(const char *path, bool *val)
{
	unsigned int tmp;
	int ret;

	ret = read_sysfs_uint(path, &tmp);
	if (ret)
		return ret;

	*val = (tmp != 0);
	return 0;
}

static int read_sysfs_ull(const char *path, unsigned long long *val)
{
	char buf[64];
	int ret;

	ret = read_sysfs_string(path, buf, sizeof(buf));
	if (ret)
		return ret;

	*val = strtoull(buf, NULL, 0);
	return 0;
}

static int write_sysfs_uint(const char *path, unsigned int val)
{
	FILE *f;

	f = fopen(path, "w");
	if (!f)
		return -errno;

	if (fprintf(f, "%u\n", val) < 0) {
		fclose(f);
		return -EIO;
	}

	fclose(f);
	return 0;
}

static int write_device_attr_uint(const char *device_path, const char *attr,
				  unsigned int val)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s", device_path, attr);
	return write_sysfs_uint(path, val);
}

/* Check if a sysfs file exists */
static bool sysfs_file_exists(const char *device_path, const char *attr)
{
	char path[MAX_PATH_LEN];
	struct stat st;

	snprintf(path, sizeof(path), "%s/%s", device_path, attr);
	return (stat(path, &st) == 0);
}

/* Read a sysfs attribute from device path */
static int read_device_attr_string(const char *device_path, const char *attr,
				   char *buf, size_t len)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s", device_path, attr);
	return read_sysfs_string(path, buf, len);
}

static int read_device_attr_uint(const char *device_path, const char *attr,
				 unsigned int *val)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s", device_path, attr);
	return read_sysfs_uint(path, val);
}

static int read_device_attr_int(const char *device_path, const char *attr,
				int *val)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s", device_path, attr);
	return read_sysfs_int(path, val);
}

static int read_device_attr_bool(const char *device_path, const char *attr,
				 bool *val)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s", device_path, attr);
	return read_sysfs_bool(path, val);
}

static int read_device_attr_ull(const char *device_path, const char *attr,
				unsigned long long *val)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s", device_path, attr);
	return read_sysfs_ull(path, val);
}

/* Parse link info from flat sysfs attributes (link0_xxx, link1_xxx, etc.) */
static int parse_link_info(const char *device_path, unsigned int link_idx,
			   struct jesd204_link_info *link)
{
	char attr[64];

	memset(link, 0, sizeof(*link));

	snprintf(attr, sizeof(attr), "link%u_link_id", link_idx);
	if (read_device_attr_uint(device_path, attr, &link->link_id) != 0)
		return -ENOENT;

	snprintf(attr, sizeof(attr), "link%u_error", link_idx);
	read_device_attr_int(device_path, attr, &link->error);

	snprintf(attr, sizeof(attr), "link%u_state", link_idx);
	read_device_attr_string(device_path, attr, link->state, sizeof(link->state));

	snprintf(attr, sizeof(attr), "link%u_fsm_paused", link_idx);
	read_device_attr_bool(device_path, attr, &link->fsm_paused);

	snprintf(attr, sizeof(attr), "link%u_fsm_ignore_errors", link_idx);
	read_device_attr_bool(device_path, attr, &link->fsm_ignore_errors);

	snprintf(attr, sizeof(attr), "link%u_sample_rate", link_idx);
	read_device_attr_ull(device_path, attr, &link->sample_rate);

	snprintf(attr, sizeof(attr), "link%u_sample_rate_div", link_idx);
	if (read_device_attr_uint(device_path, attr, &link->sample_rate_div) != 0)
		link->sample_rate_div = 1; /* Default to 1 if not present */

	snprintf(attr, sizeof(attr), "link%u_is_transmit", link_idx);
	read_device_attr_bool(device_path, attr, &link->is_transmit);

	snprintf(attr, sizeof(attr), "link%u_num_lanes", link_idx);
	read_device_attr_uint(device_path, attr, &link->num_lanes);

	snprintf(attr, sizeof(attr), "link%u_num_converters", link_idx);
	read_device_attr_uint(device_path, attr, &link->num_converters);

	snprintf(attr, sizeof(attr), "link%u_octets_per_frame", link_idx);
	read_device_attr_uint(device_path, attr, &link->octets_per_frame);

	snprintf(attr, sizeof(attr), "link%u_frames_per_multiframe", link_idx);
	read_device_attr_uint(device_path, attr, &link->frames_per_multiframe);

	snprintf(attr, sizeof(attr), "link%u_num_of_multiblocks_in_emb", link_idx);
	read_device_attr_uint(device_path, attr, &link->num_of_multiblocks_in_emb);

	snprintf(attr, sizeof(attr), "link%u_bits_per_sample", link_idx);
	read_device_attr_uint(device_path, attr, &link->bits_per_sample);

	snprintf(attr, sizeof(attr), "link%u_converter_resolution", link_idx);
	read_device_attr_uint(device_path, attr, &link->converter_resolution);

	snprintf(attr, sizeof(attr), "link%u_jesd_version", link_idx);
	read_device_attr_uint(device_path, attr, &link->jesd_version);

	snprintf(attr, sizeof(attr), "link%u_jesd_encoder", link_idx);
	read_device_attr_uint(device_path, attr, &link->jesd_encoder);

	snprintf(attr, sizeof(attr), "link%u_subclass", link_idx);
	read_device_attr_uint(device_path, attr, &link->subclass);

	snprintf(attr, sizeof(attr), "link%u_device_id", link_idx);
	read_device_attr_uint(device_path, attr, &link->device_id);

	snprintf(attr, sizeof(attr), "link%u_bank_id", link_idx);
	read_device_attr_uint(device_path, attr, &link->bank_id);

	snprintf(attr, sizeof(attr), "link%u_scrambling", link_idx);
	read_device_attr_bool(device_path, attr, &link->scrambling);

	snprintf(attr, sizeof(attr), "link%u_high_density", link_idx);
	read_device_attr_bool(device_path, attr, &link->high_density);

	snprintf(attr, sizeof(attr), "link%u_ctrl_words_per_frame_clk", link_idx);
	read_device_attr_uint(device_path, attr, &link->ctrl_words_per_frame_clk);

	snprintf(attr, sizeof(attr), "link%u_ctrl_bits_per_sample", link_idx);
	read_device_attr_uint(device_path, attr, &link->ctrl_bits_per_sample);

	snprintf(attr, sizeof(attr), "link%u_samples_per_conv_frame", link_idx);
	read_device_attr_uint(device_path, attr, &link->samples_per_conv_frame);

	return 0;
}

/* Parse input connection (in0_xxx, in1_xxx, etc.) */
static int parse_input_connection(const char *device_path, const char *device_name,
				  unsigned int con_idx, struct jesd204_connection *con)
{
	char attr[64];

	memset(con, 0, sizeof(*con));
	strncpy(con->from_device, device_name, MAX_NAME_LEN - 1);
	con->is_input = true;

	snprintf(attr, sizeof(attr), "in%u_to", con_idx);
	if (read_device_attr_string(device_path, attr, con->to_device,
				    sizeof(con->to_device)) != 0)
		return -ENOENT;

	snprintf(attr, sizeof(attr), "in%u_id", con_idx);
	read_device_attr_uint(device_path, attr, &con->con_id);

	snprintf(attr, sizeof(attr), "in%u_topo_id", con_idx);
	read_device_attr_uint(device_path, attr, &con->topo_id);

	snprintf(attr, sizeof(attr), "in%u_link_id", con_idx);
	read_device_attr_uint(device_path, attr, &con->link_id);

	snprintf(attr, sizeof(attr), "in%u_state", con_idx);
	read_device_attr_string(device_path, attr, con->state, sizeof(con->state));

	snprintf(attr, sizeof(attr), "in%u_error", con_idx);
	read_device_attr_int(device_path, attr, &con->error);

	return 0;
}

/* Parse output connection (out0_xxx, out1_xxx, etc.) */
static int parse_output_connection(const char *device_path, const char *device_name,
				   unsigned int con_idx, struct jesd204_connection *con)
{
	char attr[64];

	memset(con, 0, sizeof(*con));
	strncpy(con->from_device, device_name, MAX_NAME_LEN - 1);
	con->is_input = false;

	snprintf(attr, sizeof(attr), "out%u_to", con_idx);
	if (read_device_attr_string(device_path, attr, con->to_device,
				    sizeof(con->to_device)) != 0)
		return -ENOENT;

	snprintf(attr, sizeof(attr), "out%u_id", con_idx);
	read_device_attr_uint(device_path, attr, &con->con_id);

	snprintf(attr, sizeof(attr), "out%u_topo_id", con_idx);
	read_device_attr_uint(device_path, attr, &con->topo_id);

	snprintf(attr, sizeof(attr), "out%u_link_id", con_idx);
	read_device_attr_uint(device_path, attr, &con->link_id);

	snprintf(attr, sizeof(attr), "out%u_state", con_idx);
	read_device_attr_string(device_path, attr, con->state, sizeof(con->state));

	snprintf(attr, sizeof(attr), "out%u_error", con_idx);
	read_device_attr_int(device_path, attr, &con->error);

	return 0;
}

static int parse_device(const char *sysfs_path, const char *dev_name)
{
	struct jesd204_device *dev;
	char attr[64];
	unsigned int i;
	unsigned int num_links_attr = 0;
	int topo_id = -1;

	if (num_devices >= MAX_DEVICES) {
		fprintf(stderr, "Too many devices (max %d)\n", MAX_DEVICES);
		return -ENOMEM;
	}

	dev = &devices[num_devices];
	memset(dev, 0, sizeof(*dev));

	snprintf(dev->sysfs_name, sizeof(dev->sysfs_name), "%s", dev_name);
	if (snprintf(dev->sysfs_path, sizeof(dev->sysfs_path), "%s/%s",
		     sysfs_path, dev_name) >= (int)sizeof(dev->sysfs_path)) {
		fprintf(stderr, "Path too long: %s/%s\n", sysfs_path, dev_name);
		return -ENAMETOOLONG;
	}

	/* Read device name from sysfs */
	if (read_device_attr_string(dev->sysfs_path, "name", dev->name,
				    sizeof(dev->name)) != 0) {
		snprintf(dev->name, sizeof(dev->name), "%s", dev_name);
	}

	/* Check if this is a top device (has num_links attribute) */
	if (read_device_attr_uint(dev->sysfs_path, "num_links", &num_links_attr) == 0) {
		dev->is_top = true;
	}

	/* Read topology_id if present */
	if (read_device_attr_int(dev->sysfs_path, "topology_id", &topo_id) == 0) {
		dev->topology_id = topo_id;
	} else {
		dev->topology_id = -1;
	}

	/* Read num_retries if present */
	read_device_attr_uint(dev->sysfs_path, "num_retries", &dev->num_retries);

	/* Parse links (link0_xxx, link1_xxx, etc.) - use num_links if available */
	for (i = 0; i < (num_links_attr > 0 ? num_links_attr : MAX_LINKS); i++) {
		snprintf(attr, sizeof(attr), "link%u_link_id", i);
		if (!sysfs_file_exists(dev->sysfs_path, attr))
			break;

		if (parse_link_info(dev->sysfs_path, i, &dev->links[dev->num_links]) == 0) {
			dev->num_links++;
			dev->is_top = true;
		}
	}

	/* Parse input connections (in0_xxx, in1_xxx, etc.) */
	for (i = 0; i < MAX_CONNECTIONS; i++) {
		snprintf(attr, sizeof(attr), "in%u_to", i);
		if (!sysfs_file_exists(dev->sysfs_path, attr))
			break;

		if (parse_input_connection(dev->sysfs_path, dev->name, i,
					   &dev->input_cons[dev->num_input_cons]) == 0) {
			dev->num_input_cons++;
		}
	}

	/* Parse output connections (out0_xxx, out1_xxx, etc.) */
	for (i = 0; i < MAX_CONNECTIONS; i++) {
		snprintf(attr, sizeof(attr), "out%u_to", i);
		if (!sysfs_file_exists(dev->sysfs_path, attr))
			break;

		if (parse_output_connection(dev->sysfs_path, dev->name, i,
					    &dev->output_cons[dev->num_output_cons]) == 0) {
			dev->num_output_cons++;
		}
	}

	num_devices++;
	return 0;
}

static int scan_devices(const char *sysfs_path)
{
	struct dirent *entry;
	DIR *dir;

	dir = opendir(sysfs_path);
	if (!dir) {
		fprintf(stderr, "Failed to open %s: %s\n", sysfs_path, strerror(errno));
		fprintf(stderr, "Is the jesd204 kernel module loaded?\n");
		return -errno;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		if (entry->d_type == DT_LNK || entry->d_type == DT_DIR) {
			parse_device(sysfs_path, entry->d_name);
		}
	}

	closedir(dir);
	return 0;
}

static const char *jesd_version_str(unsigned int version)
{
	switch (version) {
	case 0: return "JESD204A";
	case 1: return "JESD204B";
	case 2: return "JESD204C";
	default: return "Unknown";
	}
}

static const char *jesd_encoder_str(unsigned int encoder)
{
	switch (encoder) {
	case 0: return "Unknown";
	case 1: return "8B/10B";
	case 2: return "64B/66B";
	case 3: return "64B/80B";
	default: return "Unknown";
	}
}

/* JESD204 version enum values */
#define JESD204_VERSION_A	0
#define JESD204_VERSION_B	1
#define JESD204_VERSION_C	2

/* JESD204 encoder enum values */
#define JESD204_ENCODER_UNKNOWN	0
#define JESD204_ENCODER_8B10B	1
#define JESD204_ENCODER_64B66B	2
#define JESD204_ENCODER_64B80B	3

/*
 * Calculate lane rate for a JESD204 link
 * Formula: lane_rate = (M * N' * encoding_n * sample_rate) / (L * encoding_d * sample_rate_div)
 * Returns 0 on success, -1 on error
 */
static int calc_lane_rate(const struct jesd204_link_info *link,
			  unsigned long long *lane_rate_hz)
{
	unsigned long long rate;
	unsigned int encoding_n, encoding_d;
	unsigned int sample_rate_div;

	if (!link->num_lanes || !link->bits_per_sample)
		return -1;

	switch (link->jesd_version) {
	case JESD204_VERSION_C:
		switch (link->jesd_encoder) {
		case JESD204_ENCODER_64B66B:
			encoding_n = 66;
			encoding_d = 64;
			break;
		case JESD204_ENCODER_8B10B:
			encoding_n = 10;
			encoding_d = 8;
			break;
		case JESD204_ENCODER_64B80B:
			encoding_n = 80;
			encoding_d = 64;
			break;
		default:
			return -1;
		}
		break;
	default:
		/* JESD204A/B use 8B/10B */
		encoding_n = 10;
		encoding_d = 8;
		break;
	}

	sample_rate_div = link->sample_rate_div ? link->sample_rate_div : 1;

	rate = (unsigned long long)link->num_converters * link->bits_per_sample *
		encoding_n * link->sample_rate;
	rate /= (unsigned long long)link->num_lanes * encoding_d * sample_rate_div;

	*lane_rate_hz = rate;
	return 0;
}

/*
 * Calculate LMFC/LEMC rate for a JESD204 link
 * LMFC = Local Multi-Frame Clock (JESD204A/B)
 * LEMC = Local Extended Multiblock Clock (JESD204C with 64B/66B or 64B/80B)
 * Returns 0 on success, -1 on error
 */
static int calc_lmfc_lemc_rate(const struct jesd204_link_info *link,
			       unsigned long long *rate_hz)
{
	unsigned long long lane_rate;
	unsigned int bkw, div;

	if (calc_lane_rate(link, &lane_rate) != 0)
		return -1;

	if (!link->octets_per_frame || !link->frames_per_multiframe)
		return -1;

	switch (link->jesd_version) {
	case JESD204_VERSION_C:
		switch (link->jesd_encoder) {
		case JESD204_ENCODER_64B66B:
			bkw = 66;
			if (link->num_of_multiblocks_in_emb) {
				div = bkw * 32 * link->num_of_multiblocks_in_emb;
			} else {
				lane_rate *= 8;
				div = bkw * link->octets_per_frame * link->frames_per_multiframe;
			}
			break;
		case JESD204_ENCODER_64B80B:
			bkw = 80;
			if (link->num_of_multiblocks_in_emb) {
				div = bkw * 32 * link->num_of_multiblocks_in_emb;
			} else {
				lane_rate *= 8;
				div = bkw * link->octets_per_frame * link->frames_per_multiframe;
			}
			break;
		case JESD204_ENCODER_8B10B:
			div = 10 * link->octets_per_frame * link->frames_per_multiframe;
			break;
		default:
			return -1;
		}
		break;
	default:
		/* JESD204A/B */
		div = 10 * link->octets_per_frame * link->frames_per_multiframe;
		break;
	}

	*rate_hz = lane_rate / div;
	return 0;
}

/*
 * Calculate device clock for a JESD204 link
 * Returns 0 on success, -1 on error
 */
static int calc_device_clock(const struct jesd204_link_info *link,
			     unsigned long long *device_clock_hz)
{
	unsigned long long lane_rate;
	unsigned int encoding_n;

	if (calc_lane_rate(link, &lane_rate) != 0)
		return -1;

	switch (link->jesd_version) {
	case JESD204_VERSION_C:
		switch (link->jesd_encoder) {
		case JESD204_ENCODER_64B66B:
			encoding_n = 66;
			break;
		case JESD204_ENCODER_8B10B:
			encoding_n = 40;
			break;
		case JESD204_ENCODER_64B80B:
			encoding_n = 80;
			break;
		default:
			return -1;
		}
		break;
	default:
		/* JESD204A/B */
		encoding_n = 40;
		break;
	}

	*device_clock_hz = lane_rate / encoding_n;
	return 0;
}

/* Format a rate value as a human-readable string with mHz precision */
static void format_rate(unsigned long long rate_hz, char *buf, size_t len)
{
	if (rate_hz >= 1000000000ULL)
		snprintf(buf, len, "%.12f GHz", rate_hz / 1e9);
	else if (rate_hz >= 1000000ULL)
		snprintf(buf, len, "%.9f MHz", rate_hz / 1e6);
	else if (rate_hz >= 1000ULL)
		snprintf(buf, len, "%.6f kHz", rate_hz / 1e3);
	else if (rate_hz > 0)
		snprintf(buf, len, "%.3f Hz", (double)rate_hz);
	else
		snprintf(buf, len, "N/A");
}

static void print_link_summary(const struct jesd204_link_info *link, int indent)
{
	char prefix[32];

	memset(prefix, ' ', indent);
	prefix[indent] = '\0';

	printf("%sLink ID: %u\n", prefix, link->link_id);
	printf("%s  State: %s%s\n", prefix, link->state,
	       link->fsm_paused ? " (paused)" : "");
	if (link->error)
		printf("%s  Error: %d\n", prefix, link->error);
	printf("%s  Direction: %s\n", prefix, link->is_transmit ? "TX" : "RX");
	printf("%s  Version: %s\n", prefix, jesd_version_str(link->jesd_version));
	printf("%s  Encoder: %s\n", prefix, jesd_encoder_str(link->jesd_encoder));
	printf("%s  Subclass: %u\n", prefix, link->subclass);
	printf("%s  Lanes (L): %u\n", prefix, link->num_lanes);
	printf("%s  Converters (M): %u\n", prefix, link->num_converters);
	printf("%s  Bits/Sample (N'): %u\n", prefix, link->bits_per_sample);
	printf("%s  Resolution (N): %u\n", prefix, link->converter_resolution);
	printf("%s  Octets/Frame (F): %u\n", prefix, link->octets_per_frame);
	printf("%s  Frames/Multiframe (K): %u\n", prefix, link->frames_per_multiframe);
	if (link->num_of_multiblocks_in_emb)
		printf("%s  Multiblocks/EMB (E): %u\n", prefix, link->num_of_multiblocks_in_emb);
	if (link->sample_rate) {
		unsigned long long effective_rate = link->sample_rate;

		if (link->sample_rate_div > 1)
			effective_rate = link->sample_rate / link->sample_rate_div;

		if (effective_rate >= 1000000000ULL)
			printf("%s  Sample Rate: %.3f GHz\n", prefix, effective_rate / 1e9);
		else if (effective_rate >= 1000000ULL)
			printf("%s  Sample Rate: %.3f MHz\n", prefix, effective_rate / 1e6);
		else
			printf("%s  Sample Rate: %llu Hz\n", prefix, effective_rate);
	}
	printf("%s  Scrambling: %s\n", prefix, link->scrambling ? "enabled" : "disabled");
	printf("%s  High Density: %s\n", prefix, link->high_density ? "yes" : "no");
	printf("%s  Device ID: %u, Bank ID: %u\n", prefix, link->device_id, link->bank_id);

	/* Calculated rates */
	if (link->sample_rate && link->num_lanes && link->bits_per_sample) {
		unsigned long long lane_rate, lmfc_lemc, device_clk;
		char rate_str[32];
		const char *lmfc_label;

		/* Determine LMFC vs LEMC label based on encoder */
		if (link->jesd_version == JESD204_VERSION_C &&
		    (link->jesd_encoder == JESD204_ENCODER_64B66B ||
		     link->jesd_encoder == JESD204_ENCODER_64B80B))
			lmfc_label = "LEMC";
		else
			lmfc_label = "LMFC";

		printf("%s  --- Calculated Rates ---\n", prefix);

		if (calc_lane_rate(link, &lane_rate) == 0) {
			format_rate(lane_rate, rate_str, sizeof(rate_str));
			printf("%s  Lane Rate: %s\n", prefix, rate_str);
		}

		if (calc_lmfc_lemc_rate(link, &lmfc_lemc) == 0) {
			format_rate(lmfc_lemc, rate_str, sizeof(rate_str));
			printf("%s  %s Rate: %s\n", prefix, lmfc_label, rate_str);
		}

		if (calc_device_clock(link, &device_clk) == 0) {
			format_rate(device_clk, rate_str, sizeof(rate_str));
			printf("%s  Device Clock: %s\n", prefix, rate_str);
		}
	}
}

static void print_connection_summary(const struct jesd204_connection *con, bool is_input)
{
	printf("    %s %u: %s %s\n",
	       is_input ? "Input" : "Output",
	       con->con_id,
	       is_input ? "<-" : "->",
	       con->to_device);
	printf("      Topology ID: %u, Link ID: %u\n", con->topo_id, con->link_id);
	printf("      State: %s", con->state);
	if (con->error)
		printf(" (error: %d)", con->error);
	printf("\n");
}

static void print_device_summary(const struct jesd204_device *dev)
{
	unsigned int i;

	printf("\n");
	printf("================================================================================\n");
	printf("Device: %s%s\n", dev->name, dev->is_top ? " [TOP DEVICE]" : "");
	printf("  Sysfs: %s\n", dev->sysfs_name);
	if (dev->topology_id >= 0)
		printf("  Topology ID: %d\n", dev->topology_id);
	printf("================================================================================\n");

	if (dev->is_top && dev->num_links > 0) {
		printf("\nJESD204 Links (%u):\n", dev->num_links);
		for (i = 0; i < dev->num_links; i++) {
			printf("  ----------------------------------------\n");
			print_link_summary(&dev->links[i], 2);
		}
	}

	if (dev->num_input_cons > 0) {
		printf("\nInput Connections (%u):\n", dev->num_input_cons);
		for (i = 0; i < dev->num_input_cons; i++) {
			print_connection_summary(&dev->input_cons[i], true);
		}
	}

	if (dev->num_output_cons > 0) {
		printf("\nOutput Connections (%u):\n", dev->num_output_cons);
		for (i = 0; i < dev->num_output_cons; i++) {
			print_connection_summary(&dev->output_cons[i], false);
		}
	}
}

static void print_summary(void)
{
	int top_devices = 0;
	int total_links = 0;
	int total_input_cons = 0;
	int total_output_cons = 0;
	int i;

	printf("\n");
	printf("################################################################################\n");
	printf("#                        JESD204 Topology Summary                             #\n");
	printf("################################################################################\n");

	/* Count statistics */
	for (i = 0; i < num_devices; i++) {
		if (devices[i].is_top)
			top_devices++;
		total_links += devices[i].num_links;
		total_input_cons += devices[i].num_input_cons;
		total_output_cons += devices[i].num_output_cons;
	}

	printf("\nStatistics:\n");
	printf("  Total Devices: %d\n", num_devices);
	printf("  Top-Level Devices: %d\n", top_devices);
	printf("  Total Links: %d\n", total_links);
	printf("  Total Input Connections: %d\n", total_input_cons);
	printf("  Total Output Connections: %d\n", total_output_cons);

	/* Print each device */
	for (i = 0; i < num_devices; i++) {
		print_device_summary(&devices[i]);
	}

	printf("\n");
}

/* Extract a simple name from the full device name for DOT node IDs */
static void get_dot_node_id(const char *name, char *node_id, size_t len)
{
	const char *p;
	char *out = node_id;
	size_t remaining = len - 1;

	/* Find the last component or use the whole name */
	p = strrchr(name, '/');
	if (p)
		p++;
	else
		p = name;

	/* Copy alphanumeric characters and underscores */
	while (*p && remaining > 0) {
		if ((*p >= 'a' && *p <= 'z') ||
		    (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') ||
		    *p == '_') {
			*out++ = *p;
			remaining--;
		} else if (*p == ',' || *p == '-' || *p == ':' || *p == '.' || *p == '@') {
			*out++ = '_';
			remaining--;
		}
		p++;
	}
	*out = '\0';

	/* Ensure non-empty */
	if (node_id[0] == '\0') {
		strncpy(node_id, "unknown", len - 1);
		node_id[len - 1] = '\0';
	}
}

/* Get a display label for a device */
static void get_dot_label(const char *name, char *label, size_t len)
{
	const char *start, *end;
	char *c;

	/* Try to extract the device path part (between slashes) and parent */
	start = name;
	if (start[0] == '/')
		start++;

	/* Find last significant part */
	end = strstr(name, ",jesd204:");
	if (end) {
		size_t copy_len = end - name;
		if (copy_len >= len)
			copy_len = len - 1;
		memcpy(label, name, copy_len);
		label[copy_len] = '\0';
	} else {
		size_t slen = strlen(name);
		if (slen >= len)
			slen = len - 1;
		memcpy(label, name, slen);
		label[slen] = '\0';
	}

	/* Escape any special characters for DOT */
	for (c = label; *c; c++) {
		if (*c == '"')
			*c = '\'';
	}
}

/* Find a device by name pattern */
static struct jesd204_device *find_device_by_name(const char *pattern)
{
	int i;

	for (i = 0; i < num_devices; i++) {
		if (strstr(devices[i].name, pattern))
			return &devices[i];
	}
	return NULL;
}

/* Get a short label for ASCII display */
static void get_short_label(const char *name, char *label, size_t len)
{
	const char *p;
	char *end;

	/* Try to find device name like "ad9084@0" or "axi-jesd204-rx@" */
	p = strstr(name, "/spi");
	if (p) {
		p = strchr(p + 1, '/');
		if (p)
			p++;
		else
			p = name;
	} else {
		p = strstr(name, "/axi");
		if (p)
			p++;
		else
			p = name;
	}

	/* Skip leading slash */
	if (*p == '/')
		p++;

	{
		size_t slen = strlen(p);
		if (slen >= len)
			slen = len - 1;
		memcpy(label, p, slen);
		label[slen] = '\0';
	}

	/* Truncate at ",jesd204:" if present */
	end = strstr(label, ",jesd204:");
	if (end)
		*end = '\0';

	/* Truncate at "@" and add back just the chip select if SPI */
	end = strchr(label, '@');
	if (end && strlen(end) > 1) {
		char cs = end[1];
		end[0] = '\0';
		if (cs >= '0' && cs <= '9') {
			size_t l = strlen(label);
			if (l + 2 < len) {
				label[l] = '@';
				label[l + 1] = cs;
				label[l + 2] = '\0';
			}
		}
	}
}

/* Structure to hold device position in ASCII graph */
struct ascii_node {
	int device_idx;
	int level;
	int column;
	bool visited;
};

/* Print ASCII topology graph */
static void print_ascii_graph(void)
{
	struct ascii_node nodes[MAX_DEVICES];
	int level_devices[32][MAX_DEVICES];  /* Device indices at each level */
	int max_level = 0;
	int level_counts[32] = {0};
	int i, j, k, level;
	char labels[MAX_DEVICES][32];
	struct jesd204_device *dev, *to_dev;
	const int box_width = 28;
	const int box_spacing = 2;

	printf("\n");
	printf("================================================================================\n");
	printf("                         JESD204 Topology Graph\n");
	printf("================================================================================\n\n");

	/* Initialize nodes */
	for (i = 0; i < num_devices; i++) {
		nodes[i].device_idx = i;
		nodes[i].level = -1;
		nodes[i].column = 0;
		nodes[i].visited = false;
	}

	/* Assign levels - TOP devices are level 0 */
	for (i = 0; i < num_devices; i++) {
		if (devices[i].is_top) {
			nodes[i].level = 0;
			nodes[i].visited = true;
		}
	}

	/* Propagate levels through connections (BFS-like) */
	for (level = 0; level < 20; level++) {
		for (i = 0; i < num_devices; i++) {
			if (nodes[i].level != level)
				continue;

			dev = &devices[i];

			/* Follow input connections to find children */
			for (j = 0; j < (int)dev->num_input_cons; j++) {
				to_dev = find_device_by_name(dev->input_cons[j].to_device);
				if (to_dev) {
					int idx = to_dev - devices;
					if (nodes[idx].level == -1) {
						nodes[idx].level = level + 1;
						nodes[idx].visited = true;
						if (level + 1 > max_level)
							max_level = level + 1;
					}
				}
			}
		}
	}

	/* Assign unvisited devices (clock sources with no incoming connections) to max_level+1 */
	for (i = 0; i < num_devices; i++) {
		if (nodes[i].level == -1) {
			nodes[i].level = max_level + 1;
			if (max_level < INT_MAX)
				max_level = max_level + 1;
		}
	}

	/* Build level_devices array and count devices per level */
	for (i = 0; i < num_devices; i++) {
		int lvl = nodes[i].level;
		if (lvl >= 0 && lvl < 32) {
			level_devices[lvl][level_counts[lvl]] = i;
			nodes[i].column = level_counts[lvl];
			level_counts[lvl]++;
		}
	}

	/* Pre-generate labels for all devices */
	for (i = 0; i < num_devices; i++) {
		get_short_label(devices[i].name, labels[i], sizeof(labels[i]));
		/* Truncate if too long for box */
		if (strlen(labels[i]) > (size_t)(box_width - 4))
			labels[i][box_width - 4] = '\0';
	}

	/* Print the graph level by level */
	for (level = 0; level <= max_level; level++) {
		int count = level_counts[level];
		int total_box_width;
		int left_margin;

		if (count == 0)
			continue;

		total_box_width = count * box_width + (count - 1) * box_spacing;
		left_margin = (80 - total_box_width) / 2;
		if (left_margin < 0)
			left_margin = 0;

		/* Print top border row for all boxes */
		for (j = 0; j < left_margin; j++)
			printf(" ");
		for (k = 0; k < count; k++) {
			printf("+");
			for (j = 0; j < box_width - 2; j++)
				printf("-");
			printf("+");
			if (k < count - 1) {
				for (j = 0; j < box_spacing; j++)
					printf(" ");
			}
		}
		printf("\n");

		/* Print label row for all boxes */
		for (j = 0; j < left_margin; j++)
			printf(" ");
		for (k = 0; k < count; k++) {
			int dev_idx = level_devices[level][k];
			printf("| %-*s |", box_width - 4, labels[dev_idx]);
			if (k < count - 1) {
				for (j = 0; j < box_spacing; j++)
					printf(" ");
			}
		}
		printf("\n");

		/* Print type row for all boxes */
		for (j = 0; j < left_margin; j++)
			printf(" ");
		for (k = 0; k < count; k++) {
			int dev_idx = level_devices[level][k];
			dev = &devices[dev_idx];
			const char *type_str;
			if (dev->is_top)
				type_str = "[TOP]";
			else if (dev->num_input_cons == 0 && dev->num_output_cons == 0)
				type_str = "[CLK]";
			else
				type_str = "";
			printf("| %-*s |", box_width - 4, type_str);
			if (k < count - 1) {
				for (j = 0; j < box_spacing; j++)
					printf(" ");
			}
		}
		printf("\n");

		/* Print bottom border row for all boxes */
		for (j = 0; j < left_margin; j++)
			printf(" ");
		for (k = 0; k < count; k++) {
			printf("+");
			for (j = 0; j < box_width - 2; j++)
				printf("-");
			printf("+");
			if (k < count - 1) {
				for (j = 0; j < box_spacing; j++)
					printf(" ");
			}
		}
		printf("\n");

		/* Print connection arrows to next level */
		if (level < max_level) {
			int arrow_positions[MAX_DEVICES];
			int num_arrows = 0;

			/* Calculate arrow positions based on box centers */
			for (k = 0; k < count; k++) {
				int dev_idx = level_devices[level][k];
				dev = &devices[dev_idx];
				if (dev->num_input_cons > 0) {
					int box_center = left_margin + k * (box_width + box_spacing) + box_width / 2;
					arrow_positions[num_arrows++] = box_center;
				}
			}

			if (num_arrows > 0) {
				/* Print vertical lines */
				char line[256];
				memset(line, ' ', sizeof(line) - 1);
				line[255] = '\0';
				for (k = 0; k < num_arrows; k++) {
					if (arrow_positions[k] < 255)
						line[arrow_positions[k]] = '|';
				}
				/* Trim trailing spaces */
				for (j = 254; j >= 0 && line[j] == ' '; j--)
					line[j] = '\0';
				printf("%s\n", line);

				/* Print arrow heads */
				memset(line, ' ', sizeof(line) - 1);
				line[255] = '\0';
				for (k = 0; k < num_arrows; k++) {
					if (arrow_positions[k] < 255)
						line[arrow_positions[k]] = 'v';
				}
				for (j = 254; j >= 0 && line[j] == ' '; j--)
					line[j] = '\0';
				printf("%s\n", line);
			}
		}

		printf("\n");
	}

	/* Print legend */
	printf("Legend: [TOP] = Top device (ADC/DAC)  [CLK] = Clock/SYSREF source\n");
	printf("\n");

	/* Print link parameters for all TOP devices */
	for (i = 0; i < num_devices; i++) {
		dev = &devices[i];
		if (!dev->is_top || dev->num_links == 0)
			continue;

		for (j = 0; j < (int)dev->num_links; j++) {
			struct jesd204_link_info *link = &dev->links[j];
			char sample_rate_str[32];
			char lane_rate_str[32];
			char lmfc_lemc_str[32];
			char device_clk_str[32];
			unsigned long long effective_rate = link->sample_rate;
			unsigned long long lane_rate, lmfc_lemc, device_clk;
			const char *lmfc_label;

			/* Apply sample rate divider if present */
			if (link->sample_rate_div > 1)
				effective_rate = link->sample_rate / link->sample_rate_div;

			/* Format sample rate */
			format_rate(effective_rate, sample_rate_str, sizeof(sample_rate_str));

			/* Calculate and format lane rate */
			if (calc_lane_rate(link, &lane_rate) == 0)
				format_rate(lane_rate, lane_rate_str, sizeof(lane_rate_str));
			else
				snprintf(lane_rate_str, sizeof(lane_rate_str), "N/A");

			/* Calculate and format LMFC/LEMC rate */
			if (calc_lmfc_lemc_rate(link, &lmfc_lemc) == 0)
				format_rate(lmfc_lemc, lmfc_lemc_str, sizeof(lmfc_lemc_str));
			else
				snprintf(lmfc_lemc_str, sizeof(lmfc_lemc_str), "N/A");

			/* Calculate and format device clock */
			if (calc_device_clock(link, &device_clk) == 0)
				format_rate(device_clk, device_clk_str, sizeof(device_clk_str));
			else
				snprintf(device_clk_str, sizeof(device_clk_str), "N/A");

			/* Determine LMFC vs LEMC label based on encoder */
			if (link->jesd_version == JESD204_VERSION_C &&
			    (link->jesd_encoder == JESD204_ENCODER_64B66B ||
			     link->jesd_encoder == JESD204_ENCODER_64B80B))
				lmfc_label = "LEMC";
			else
				lmfc_label = "LMFC";

			printf("--------------------------------------------------------------------------------\n");
			printf("Link %u - %s (%s)  State: %s%s\n",
			       link->link_id,
			       link->is_transmit ? "TX" : "RX",
			       jesd_version_str(link->jesd_version),
			       link->state,
			       link->fsm_paused ? " (paused)" : "");
			printf("--------------------------------------------------------------------------------\n");
			printf("  JESD Parameters:  L=%u  M=%u  N=%u  N'=%u  F=%u  K=%u  S=%u",
			       link->num_lanes,
			       link->num_converters,
			       link->converter_resolution,
			       link->bits_per_sample,
			       link->octets_per_frame,
			       link->frames_per_multiframe,
			       link->samples_per_conv_frame);
			if (link->num_of_multiblocks_in_emb)
				printf("  E=%u", link->num_of_multiblocks_in_emb);
			printf("\n");
			printf("  Encoder: %-8s  Subclass: %u  Scrambling: %-3s  HD: %s\n",
			       jesd_encoder_str(link->jesd_encoder),
			       link->subclass,
			       link->scrambling ? "Yes" : "No",
			       link->high_density ? "Yes" : "No");
			printf("  Sample Rate:  %s\n", sample_rate_str);
			printf("  Lane Rate:    %s\n", lane_rate_str);
			printf("  %s Rate:   %s\n", lmfc_label, lmfc_lemc_str);
			printf("  Device Clock: %s\n", device_clk_str);
			printf("\n");
		}
	}
}

static int generate_dot_file(const char *filename)
{
	FILE *f;
	int i, j;
	char from_id[MAX_NAME_LEN];
	char to_id[MAX_NAME_LEN];
	char label[MAX_NAME_LEN];
	struct jesd204_device *to_dev;

	f = fopen(filename, "w");
	if (!f) {
		fprintf(stderr, "Failed to create DOT file: %s\n", strerror(errno));
		return -errno;
	}

	fprintf(f, "// JESD204 Topology Graph\n");
	fprintf(f, "// Generated by jesd204-topology tool\n");
	fprintf(f, "// Visualize with: dot -Tpng %s -o topology.png\n\n", filename);

	fprintf(f, "digraph jesd204_topology {\n");
	fprintf(f, "    rankdir=TB;\n");
	fprintf(f, "    node [shape=box, style=filled, fontname=\"Helvetica\"];\n");
	fprintf(f, "    edge [fontsize=10, fontname=\"Helvetica\"];\n\n");

	/* Define nodes for all devices */
	fprintf(f, "    // Device nodes\n");
	for (i = 0; i < num_devices; i++) {
		get_dot_node_id(devices[i].name, from_id, sizeof(from_id));
		get_dot_label(devices[i].name, label, sizeof(label));

		if (devices[i].is_top) {
			/* Top devices with links are highlighted */
			fprintf(f, "    %s [label=\"%s\\n[TOP - %u links]\", fillcolor=\"#90EE90\", penwidth=2];\n",
				from_id, label, devices[i].num_links);
		} else if (devices[i].num_input_cons > 0 || devices[i].num_output_cons > 0) {
			/* Devices with connections */
			fprintf(f, "    %s [label=\"%s\", fillcolor=\"#ADD8E6\"];\n",
				from_id, label);
		} else {
			/* Clock/SYSREF provider - terminating device at bottom of chain */
			fprintf(f, "    %s [label=\"%s\\n[CLK/SYSREF]\", fillcolor=\"#FFD700\"];\n",
				from_id, label);
		}
	}

	fprintf(f, "\n    // Connections (TOP device at top, arrows point down showing signal flow)\n");

	/*
	 * The input connections in sysfs show "this device receives from X".
	 * For the DOT file, we want TOP device at top with arrows pointing DOWN.
	 * So we draw: this_device -> source_device (reversed from input direction)
	 * This puts TOP at top since it has inputs from devices below it.
	 */
	for (i = 0; i < num_devices; i++) {
		get_dot_node_id(devices[i].name, from_id, sizeof(from_id));

		for (j = 0; j < (int)devices[i].num_input_cons; j++) {
			struct jesd204_connection *con = &devices[i].input_cons[j];

			/* Find the source device (the one this device receives from) */
			to_dev = find_device_by_name(con->to_device);
			if (to_dev) {
				get_dot_node_id(to_dev->name, to_id, sizeof(to_id));
			} else {
				get_dot_node_id(con->to_device, to_id, sizeof(to_id));
			}

			/* Edge from this device DOWN to its input source */
			fprintf(f, "    %s -> %s [label=\"L%u\", ",
				from_id, to_id, con->link_id);

			/* Color based on state/error */
			if (con->error) {
				fprintf(f, "color=\"red\", penwidth=2");
			} else if (strstr(con->state, "running") || strstr(con->state, "RUNNING")) {
				fprintf(f, "color=\"#008000\", penwidth=1.5");
			} else if (strstr(con->state, "idle") || strstr(con->state, "IDLE")) {
				fprintf(f, "color=\"#808080\"");
			} else {
				fprintf(f, "color=\"#404040\"");
			}
			fprintf(f, "];\n");
		}
	}

	/* Legend */
	fprintf(f, "\n    // Legend\n");
	fprintf(f, "    subgraph cluster_legend {\n");
	fprintf(f, "        label=\"Legend\";\n");
	fprintf(f, "        fontname=\"Helvetica\";\n");
	fprintf(f, "        style=rounded;\n");
	fprintf(f, "        color=\"#808080\";\n");
	fprintf(f, "        legend_top [label=\"Top Device\\n(ADC/DAC)\", fillcolor=\"#90EE90\", penwidth=2];\n");
	fprintf(f, "        legend_dev [label=\"JESD204\\nDevice\", fillcolor=\"#ADD8E6\"];\n");
	fprintf(f, "        legend_clk [label=\"CLK/SYSREF\\nProvider\", fillcolor=\"#FFD700\"];\n");
	fprintf(f, "        legend_top -> legend_dev [style=invis];\n");
	fprintf(f, "        legend_dev -> legend_clk [style=invis];\n");
	fprintf(f, "    }\n");

	/* Link parameter boxes - one for each active link from top devices */
	fprintf(f, "\n    // Link Parameters\n");
	for (i = 0; i < num_devices; i++) {
		if (!devices[i].is_top || devices[i].num_links == 0)
			continue;

		for (j = 0; j < (int)devices[i].num_links; j++) {
			struct jesd204_link_info *link = &devices[i].links[j];
			char sample_rate_str[32];
			char lane_rate_str[32];
			char lmfc_lemc_str[32];
			char device_clk_str[32];
			unsigned long long effective_rate = link->sample_rate;
			unsigned long long lane_rate, lmfc_lemc, device_clk;
			const char *lmfc_label;

			/* Apply sample rate divider if present */
			if (link->sample_rate_div > 1)
				effective_rate = link->sample_rate / link->sample_rate_div;

			/* Format sample rate */
			format_rate(effective_rate, sample_rate_str, sizeof(sample_rate_str));

			/* Calculate and format lane rate */
			if (calc_lane_rate(link, &lane_rate) == 0)
				format_rate(lane_rate, lane_rate_str, sizeof(lane_rate_str));
			else
				snprintf(lane_rate_str, sizeof(lane_rate_str), "N/A");

			/* Calculate and format LMFC/LEMC rate */
			if (calc_lmfc_lemc_rate(link, &lmfc_lemc) == 0)
				format_rate(lmfc_lemc, lmfc_lemc_str, sizeof(lmfc_lemc_str));
			else
				snprintf(lmfc_lemc_str, sizeof(lmfc_lemc_str), "N/A");

			/* Calculate and format device clock */
			if (calc_device_clock(link, &device_clk) == 0)
				format_rate(device_clk, device_clk_str, sizeof(device_clk_str));
			else
				snprintf(device_clk_str, sizeof(device_clk_str), "N/A");

			/* Determine LMFC vs LEMC label based on encoder */
			if (link->jesd_version == JESD204_VERSION_C &&
			    (link->jesd_encoder == JESD204_ENCODER_64B66B ||
			     link->jesd_encoder == JESD204_ENCODER_64B80B))
				lmfc_label = "LEMC";
			else
				lmfc_label = "LMFC";

			fprintf(f, "    subgraph cluster_link%u {\n", link->link_id);
			fprintf(f, "        label=\"Link %u Parameters\";\n", link->link_id);
			fprintf(f, "        fontname=\"Helvetica\";\n");
			fprintf(f, "        style=rounded;\n");
			fprintf(f, "        color=\"%s\";\n",
				link->error ? "#FF0000" : "#008000");
			fprintf(f, "        link%u_params [shape=none, margin=0, label=<\n",
				link->link_id);
			fprintf(f, "            <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n");
			fprintf(f, "            <TR><TD COLSPAN=\"2\" BGCOLOR=\"%s\"><B>Link %u - %s</B></TD></TR>\n",
				link->is_transmit ? "#FFB6C1" : "#B6D0FF",
				link->link_id,
				link->is_transmit ? "TX" : "RX");
			fprintf(f, "            <TR><TD>State</TD><TD>%s%s</TD></TR>\n",
				link->state, link->fsm_paused ? " (paused)" : "");
			fprintf(f, "            <TR><TD>Version</TD><TD>%s</TD></TR>\n",
				jesd_version_str(link->jesd_version));
			fprintf(f, "            <TR><TD>Encoder</TD><TD>%s</TD></TR>\n",
				jesd_encoder_str(link->jesd_encoder));
			fprintf(f, "            <TR><TD>Subclass</TD><TD>%u</TD></TR>\n",
				link->subclass);
			fprintf(f, "            <TR><TD>Sample Rate</TD><TD>%s</TD></TR>\n",
				sample_rate_str);
			fprintf(f, "            <TR><TD COLSPAN=\"2\" BGCOLOR=\"#E8E8E8\"><B>JESD204 Parameters</B></TD></TR>\n");
			fprintf(f, "            <TR><TD>L (Lanes)</TD><TD>%u</TD></TR>\n",
				link->num_lanes);
			fprintf(f, "            <TR><TD>M (Converters)</TD><TD>%u</TD></TR>\n",
				link->num_converters);
			fprintf(f, "            <TR><TD>N (Resolution)</TD><TD>%u</TD></TR>\n",
				link->converter_resolution);
			fprintf(f, "            <TR><TD>N' (Bits/Sample)</TD><TD>%u</TD></TR>\n",
				link->bits_per_sample);
			fprintf(f, "            <TR><TD>F (Octets/Frame)</TD><TD>%u</TD></TR>\n",
				link->octets_per_frame);
			fprintf(f, "            <TR><TD>K (Frames/MF)</TD><TD>%u</TD></TR>\n",
				link->frames_per_multiframe);
			fprintf(f, "            <TR><TD>S (Samples/Conv)</TD><TD>%u</TD></TR>\n",
				link->samples_per_conv_frame);
			if (link->num_of_multiblocks_in_emb)
				fprintf(f, "            <TR><TD>E (MBlocks/EMB)</TD><TD>%u</TD></TR>\n",
					link->num_of_multiblocks_in_emb);
			fprintf(f, "            <TR><TD>Scrambling</TD><TD>%s</TD></TR>\n",
				link->scrambling ? "Yes" : "No");
			fprintf(f, "            <TR><TD>High Density</TD><TD>%s</TD></TR>\n",
				link->high_density ? "Yes" : "No");
			fprintf(f, "            <TR><TD COLSPAN=\"2\" BGCOLOR=\"#D4E8D4\"><B>Calculated Rates</B></TD></TR>\n");
			fprintf(f, "            <TR><TD>Lane Rate</TD><TD>%s</TD></TR>\n",
				lane_rate_str);
			fprintf(f, "            <TR><TD>%s Rate</TD><TD>%s</TD></TR>\n",
				lmfc_label, lmfc_lemc_str);
			fprintf(f, "            <TR><TD>Device Clock</TD><TD>%s</TD></TR>\n",
				device_clk_str);
			fprintf(f, "            </TABLE>\n");
			fprintf(f, "        >];\n");
			fprintf(f, "    }\n");
		}
	}

	fprintf(f, "}\n");

	fclose(f);

	printf("DOT file generated: %s\n", filename);
	printf("To visualize, run:\n");
	printf("  dot -Tpng %s -o topology.png\n", filename);
	printf("  dot -Tsvg %s -o topology.svg\n", filename);

	return 0;
}

static int parse_options(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"help",             no_argument,       NULL, 'h'},
		{"verbose",          no_argument,       NULL, 'v'},
		{"graph",            no_argument,       NULL, 'g'},
		{"dot",              required_argument, NULL, 'd'},
		{"path",             required_argument, NULL, 'p'},
		{"ignore-errors",    optional_argument, NULL, 'i'},
		{"no-ignore-errors", optional_argument, NULL, 'I'},
		{NULL,               0,                 NULL, 0}
	};
	int opt;

	while ((opt = getopt_long(argc, argv, "hvgd:p:i::I::", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			print_usage(argv[0]);
			exit(0);
		case 'v':
			options.verbose = true;
			break;
		case 'g':
			options.ascii_graph = true;
			break;
		case 'i':
			options.set_ignore_errors = true;
			if (optarg)
				options.ignore_errors_link = atoi(optarg);
			else if (optind < argc && argv[optind] && argv[optind][0] != '-')
				options.ignore_errors_link = atoi(argv[optind++]);
			break;
		case 'I':
			options.clear_ignore_errors = true;
			if (optarg)
				options.ignore_errors_link = atoi(optarg);
			else if (optind < argc && argv[optind] && argv[optind][0] != '-')
				options.ignore_errors_link = atoi(argv[optind++]);
			break;
		case 'd':
			options.generate_dot = true;
			strncpy(options.dot_filename, optarg, sizeof(options.dot_filename) - 1);
			break;
		case 'p':
			strncpy(options.sysfs_path, optarg, sizeof(options.sysfs_path) - 1);
			break;
		default:
			print_usage(argv[0]);
			return -EINVAL;
		}
	}

	return 0;
}

static int set_ignore_errors(bool value, int link_id)
{
	int i, j;
	int modified = 0;
	char attr[64];

	for (i = 0; i < num_devices; i++) {
		struct jesd204_device *dev = &devices[i];

		if (!dev->is_top || dev->num_links == 0)
			continue;

		for (j = 0; j < (int)dev->num_links; j++) {
			struct jesd204_link_info *link = &dev->links[j];

			/* Skip if a specific link was requested and this isn't it */
			if (link_id >= 0 && (int)link->link_id != link_id)
				continue;

			snprintf(attr, sizeof(attr), "link%u_fsm_ignore_errors", j);
			if (sysfs_file_exists(dev->sysfs_path, attr)) {
				int ret = write_device_attr_uint(dev->sysfs_path, attr,
								 value ? 1 : 0);
				if (ret == 0) {
					printf("%s link%u_fsm_ignore_errors on %s\n",
					       value ? "Set" : "Cleared",
					       link->link_id, dev->name);
					modified++;
				} else {
					fprintf(stderr, "Failed to %s link%u_fsm_ignore_errors on %s: %s\n",
						value ? "set" : "clear",
						link->link_id, dev->name, strerror(-ret));
				}
			}
		}
	}

	if (modified == 0) {
		if (link_id >= 0)
			fprintf(stderr, "No link with ID %d found\n", link_id);
		else
			fprintf(stderr, "No links found to modify\n");
		return -ENOENT;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = parse_options(argc, argv);
	if (ret)
		return 1;

	if (options.verbose)
		printf("Scanning JESD204 devices in %s...\n", options.sysfs_path);

	ret = scan_devices(options.sysfs_path);
	if (ret)
		return 1;

	if (num_devices == 0) {
		printf("No JESD204 devices found.\n");
		printf("Make sure the jesd204 kernel module is loaded and devices are configured.\n");
		return 0;
	}

	/* Handle ignore_errors set/clear */
	if (options.set_ignore_errors) {
		ret = set_ignore_errors(true, options.ignore_errors_link);
		return ret ? 1 : 0;
	}

	if (options.clear_ignore_errors) {
		ret = set_ignore_errors(false, options.ignore_errors_link);
		return ret ? 1 : 0;
	}

	print_summary();

	if (options.ascii_graph)
		print_ascii_graph();

	if (options.generate_dot) {
		ret = generate_dot_file(options.dot_filename);
		if (ret)
			return 1;
	}

	return 0;
}
