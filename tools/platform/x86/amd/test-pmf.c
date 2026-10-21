// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Platform Management Framework Test Tool
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *          Sanket Goswami <Sanket.Goswami@amd.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/amd-pmf.h>
#include <linux/kernel.h>

#define DEVICE_NODE	"/dev/amdpmf_interface"

/* Feature flag names */
static const char * const feature_names[] = {
	"Auto Mode",
	"Static Power Slider",
	"Policy Builder (Smart PC)",
	"Dynamic Power Slider AC",
	"Dynamic Power Slider DC",
};

static const char *banner =
	"====================================================\n"
	"      AMD PMF Metrics info and Feature Status\n"
	"====================================================\n\n";

/* Print feature flags */
static void pmf_print_features(uint32_t flags)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(feature_names); i++)
		printf("  [%c] %s\n", (flags & (1U << i)) ? 'x' : ' ', feature_names[i]);
}

/* Print BIOS parameters */
static void pmf_print_bios_params(const char *type, const __u32 *params)
{
	int i;

	for (i = 0; i < AMD_PMF_BIOS_PARAMS_MAX; i++)
		printf("  Custom BIOS %s%d: %u\n", type, i + 1, params[i]);
}

/* Open the PMF device */
static int pmf_open_device(void)
{
	int fd;

	fd = open(DEVICE_NODE, O_RDONLY);
	if (fd < 0)
		fprintf(stderr, "Error: Cannot open %s: %s\n", DEVICE_NODE, strerror(errno));

	return fd;
}

/* Query PMF info using the single IOCTL */
static int pmf_get_info(int fd, struct amd_pmf_info *info)
{
	int ret;

	/* Zero-initialize and set size for versioning */
	memset(info, 0, sizeof(*info));
	info->size = sizeof(*info);

	ret = ioctl(fd, IOCTL_AMD_PMF_POPULATE_DATA, info);
	if (ret < 0) {
		fprintf(stderr, "Error: IOCTL_AMD_PMF_POPULATE_DATA failed: %s\n", strerror(errno));
		return ret;
	}

	return 0;
}

static void pmf_print_info(const struct amd_pmf_info *info)
{
	printf("%s", banner);

	/* Feature status */
	printf("Feature Status:\n");
	pmf_print_features(info->features_supported);

	/* Device states */
	printf("\nDevice States:\n");
	printf("  Platform Type:     %s\n", amd_pmf_get_platform_type(info->platform_type));
	printf("  Laptop Placement:  %s\n", amd_pmf_get_laptop_placement(info->laptop_placement));
	printf("  Lid State:         %s\n", info->lid_state ? "Closed" : "Open");
	printf("  User Presence:     %s\n", info->user_presence ? "Present" : "Away");
	printf("  Slider Position:   %s\n", amd_pmf_get_slider_position(info->slider_position));

	/* Thermal and power metrics */
	printf("\nThermal/Power Metrics:\n");
	printf("  Skin Temperature:  %d\n", info->skin_temp / 100);
	printf("  GFX Busy:          %u\n", info->gfx_busy);
	printf("  Ambient Light:     %d\n", info->ambient_light);
	printf("  Avg C0 Residency:  %u\n", info->avg_c0_residency);
	printf("  Max C0 Residency:  %u\n", info->max_c0_residency);
	printf("  Socket Power:      %u\n", info->socket_power);

	/* BIOS parameters */
	printf("\nCustom BIOS Input Parameters:\n");
	pmf_print_bios_params("Input", info->bios_input);
	printf("\nCustom BIOS Output Parameters:\n");
	pmf_print_bios_params("Output", info->bios_output);

	printf("\n=================================================\n");
}

int main(void)
{
	struct amd_pmf_info info;
	int fd, ret;

	fd = pmf_open_device();
	if (fd < 0)
		return -1;

	/* Query all info with single IOCTL */
	ret = pmf_get_info(fd, &info);
	close(fd);

	if (ret < 0)
		return -1;

	pmf_print_info(&info);

	return 0;
}
