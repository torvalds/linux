/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wifi Band Exclusion Interface (AMD ACPI Implementation)
 * Copyright (C) 2023 Advanced Micro Devices
 */

#ifndef _ACPI_AMD_WBRF_H
#define _ACPI_AMD_WBRF_H

#include <linux/device.h>
#include <linux/notifier.h>

/* The maximum number of frequency band ranges */
#define MAX_NUM_OF_WBRF_RANGES		11

/* Record actions */
#define WBRF_RECORD_ADD		0x0
#define WBRF_RECORD_REMOVE	0x1

/**
 * struct freq_band_range - Wifi frequency band range definition
 * @start: start frequency point (in Hz)
 * @end: end frequency point (in Hz)
 */
struct freq_band_range {
	u64		start;
	u64		end;
};

/**
 * struct wbrf_ranges_in_out - wbrf ranges info
 * @num_of_ranges: total number of band ranges in this struct
 * @band_list: array of Wifi band ranges
 */
struct wbrf_ranges_in_out {
	u64			num_of_ranges;
	struct freq_band_range	band_list[MAX_NUM_OF_WBRF_RANGES];
};

/**
 * enum wbrf_notifier_actions - wbrf notifier actions index
 * @WBRF_CHANGED: there was some frequency band updates. The consumers
 *               should retrieve the latest active frequency bands.
 */
enum wbrf_notifier_actions {
	WBRF_CHANGED,
};

#if IS_ENABLED(CONFIG_AMD_WBRF)
bool acpi_amd_wbrf_supported_producer(struct device *dev);
int acpi_amd_wbrf_add_remove(struct device *dev, uint8_t action, struct wbrf_ranges_in_out *in);
bool acpi_amd_wbrf_supported_consumer(struct device *dev);
int amd_wbrf_retrieve_freq_band(struct device *dev, struct wbrf_ranges_in_out *out);
int amd_wbrf_register_notifier(struct notifier_block *nb);
int amd_wbrf_unregister_notifier(struct notifier_block *nb);
#else
static inline
bool acpi_amd_wbrf_supported_consumer(struct device *dev)
{
	return false;
}

static inline
int acpi_amd_wbrf_add_remove(struct device *dev, uint8_t action, struct wbrf_ranges_in_out *in)
{
	return -ENODEV;
}

static inline
bool acpi_amd_wbrf_supported_producer(struct device *dev)
{
	return false;
}
static inline
int amd_wbrf_retrieve_freq_band(struct device *dev, struct wbrf_ranges_in_out *out)
{
	return -ENODEV;
}
static inline
int amd_wbrf_register_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}
static inline
int amd_wbrf_unregister_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}
#endif /* CONFIG_AMD_WBRF */

#endif /* _ACPI_AMD_WBRF_H */
