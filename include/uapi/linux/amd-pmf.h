/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/*
 * AMD Platform Management Framework (PMF) UAPI Header
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * This file defines the user-space API for interacting with the AMD PMF
 * driver. It provides ioctl interfaces to query platform-specific metrics
 * such as power source, slider position, platform type, laptop placement,
 * and various BIOS input/output parameters.
 */

#ifndef _UAPI_LINUX_AMD_PMF_H
#define _UAPI_LINUX_AMD_PMF_H

#include <linux/bits.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * AMD_PMF_IOC_MAGIC - Magic number for AMD PMF ioctl commands
 *
 * This magic number uniquely identifies AMD PMF ioctl operations.
 */
#define AMD_PMF_IOC_MAGIC	'p'

/**
 * IOCTL_AMD_PMF_POPULATE_DATA - ioctl command to retrieve PMF metrics data
 *
 * This ioctl command is used to populate the amd_pmf_info structure
 * with the requested PMF metrics information.
 */
#define IOCTL_AMD_PMF_POPULATE_DATA		_IOWR(AMD_PMF_IOC_MAGIC, 0x00, __u64)

#define AMD_PMF_BIOS_PARAMS_MAX		10

/* AMD PMF feature flags - bitmask indicating supported features */
#define AMD_PMF_FEAT_AUTO_MODE			BIT(0)
#define AMD_PMF_FEAT_STATIC_POWER_SLIDER	BIT(1)
#define AMD_PMF_FEAT_POLICY_BUILDER		BIT(2)
#define AMD_PMF_FEAT_DYNAMIC_POWER_SLIDER_AC	BIT(3)
#define AMD_PMF_FEAT_DYNAMIC_POWER_SLIDER_DC	BIT(4)

/**
 * enum amd_pmf_laptop_placement - Describes the physical placement of the laptop
 * @AMD_PMF_LP_UNKNOWN: Placement cannot be determined
 * @AMD_PMF_ON_TABLE: Laptop is placed on a stable surface like a table or desk
 * @AMD_PMF_ON_LAP_MOTION: Laptop is on a lap with detected motion
 * @AMD_PMF_IN_BAG: Laptop is detected to be inside a bag or case
 * @AMD_PMF_OUT_OF_BAG: Laptop has been removed from bag or case
 * @AMD_PMF_LP_UNDEFINED: Placement state is undefined
 *
 * This enumeration represents the physical placement state of the laptop
 * as detected by platform sensors. Used for adaptive power management
 * and thermal policies.
 */
enum amd_pmf_laptop_placement {
	AMD_PMF_LP_UNKNOWN,
	AMD_PMF_ON_TABLE,
	AMD_PMF_ON_LAP_MOTION,
	AMD_PMF_IN_BAG,
	AMD_PMF_OUT_OF_BAG,
	AMD_PMF_LP_UNDEFINED,
};

/**
 * enum amd_pmf_ta_slider - Trusted Application power slider positions
 * @AMD_PMF_TA_BEST_BATTERY: Maximum battery savings, minimal performance
 * @AMD_PMF_TA_BETTER_BATTERY: Balanced towards battery life
 * @AMD_PMF_TA_BETTER_PERFORMANCE: Balanced towards performance
 * @AMD_PMF_TA_BEST_PERFORMANCE: Maximum performance, higher power consumption
 * @AMD_PMF_TA_MAX: Sentinel value indicating maximum enum value
 *
 * This enumeration defines the power slider positions used by the
 * AMD PMF Trusted Application for dynamic power management decisions.
 * These correspond to the Windows power slider UI positions.
 */
enum amd_pmf_ta_slider {
	AMD_PMF_TA_BEST_BATTERY,
	AMD_PMF_TA_BETTER_BATTERY,
	AMD_PMF_TA_BETTER_PERFORMANCE,
	AMD_PMF_TA_BEST_PERFORMANCE,
	AMD_PMF_TA_MAX,
};

/**
 * enum amd_pmf_platform_type - Describes the physical form factor orientation
 * @AMD_PMF_PTYPE_UNKNOWN: Platform type cannot be determined
 * @AMD_PMF_LID_CLOSE: Laptop lid is closed
 * @AMD_PMF_CLAMSHELL: Traditional laptop mode with keyboard and screen
 * @AMD_PMF_FLAT: Device is lying flat on a surface
 * @AMD_PMF_TENT: Device is in tent mode (keyboard folded back, standing)
 * @AMD_PMF_STAND: Device is propped up in stand orientation
 * @AMD_PMF_TABLET: Device is in tablet mode with keyboard hidden
 * @AMD_PMF_BOOK: Device is in book reading orientation
 * @AMD_PMF_PRESENTATION: Device is in presentation mode
 * @AMD_PMF_PULL_FWD: Screen is pulled forward towards user
 * @AMD_PMF_PTYPE_INVALID: Invalid platform type marker
 *
 * This enumeration describes the current physical orientation or form
 * factor of convertible/2-in-1 devices. Used for optimizing power and
 * thermal management based on device posture.
 */
enum amd_pmf_platform_type {
	AMD_PMF_PTYPE_UNKNOWN,
	AMD_PMF_LID_CLOSE,
	AMD_PMF_CLAMSHELL,
	AMD_PMF_FLAT,
	AMD_PMF_TENT,
	AMD_PMF_STAND,
	AMD_PMF_TABLET,
	AMD_PMF_BOOK,
	AMD_PMF_PRESENTATION,
	AMD_PMF_PULL_FWD,
	AMD_PMF_PTYPE_INVALID = 0xf,
};

static inline const char *amd_pmf_get_platform_type(unsigned int platform_type)
{
	switch (platform_type) {
	case AMD_PMF_CLAMSHELL:
		return "CLAMSHELL";
	case AMD_PMF_LID_CLOSE:
		return "LID_CLOSE";
	case AMD_PMF_FLAT:
		return "FLAT";
	case AMD_PMF_TENT:
		return "TENT";
	case AMD_PMF_STAND:
		return "STAND";
	case AMD_PMF_TABLET:
		return "TABLET";
	case AMD_PMF_BOOK:
		return "BOOK";
	case AMD_PMF_PRESENTATION:
		return "PRESENTATION";
	case AMD_PMF_PULL_FWD:
		return "PULL_FWD";
	default:
		return "UNKNOWN";
	}
}

static inline const char *amd_pmf_get_laptop_placement(unsigned int device_state)
{
	switch (device_state) {
	case AMD_PMF_ON_TABLE:
		return "ON_TABLE";
	case AMD_PMF_ON_LAP_MOTION:
		return "ON_LAP_MOTION";
	case AMD_PMF_IN_BAG:
		return "IN_BAG";
	case AMD_PMF_OUT_OF_BAG:
		return "OUT_OF_BAG";
	default:
		return "UNKNOWN";
	}
}

static inline const char *amd_pmf_get_slider_position(unsigned int state)
{
	switch (state) {
	case AMD_PMF_TA_BEST_PERFORMANCE:
		return "PERFORMANCE";
	case AMD_PMF_TA_BETTER_PERFORMANCE:
		return "BALANCED";
	case AMD_PMF_TA_BEST_BATTERY:
		return "POWER_SAVER";
	case AMD_PMF_TA_BETTER_BATTERY:
		return "BALANCED_BATTERY";
	default:
		return "Unknown TA Slider State";
	}
}

struct amd_pmf_info {
	__u64 size;

	/* Feature info */
	__u32 features_supported;

	/* Power and state info */
	__u32 platform_type;
	__u32 power_source;
	__u32 laptop_placement;
	__u32 lid_state;
	__u32 user_presence;
	__u32 slider_position;

	/* Thermal and power metrics */
	__s32 skin_temp;
	__u32 gfx_busy;
	__s32 ambient_light;
	__u32 avg_c0_residency;
	__u32 max_c0_residency;
	__u32 socket_power;

	/* BIOS parameters */
	__u32 bios_input[AMD_PMF_BIOS_PARAMS_MAX];
	__u32 bios_output[AMD_PMF_BIOS_PARAMS_MAX];
};

#endif /* _UAPI_LINUX_AMD_PMF_H */
