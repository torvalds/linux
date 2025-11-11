/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ARM Mali-C55 ISP Driver - Userspace API
 *
 * Copyright (C) 2023 Ideas on Board Oy
 */

#ifndef __UAPI_MALI_C55_CONFIG_H
#define __UAPI_MALI_C55_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define V4L2_CID_MALI_C55_CAPABILITIES	(V4L2_CID_USER_MALI_C55_BASE + 0x0)
#define MALI_C55_GPS_PONG		(1U << 0)
#define MALI_C55_GPS_WDR		(1U << 1)
#define MALI_C55_GPS_COMPRESSION	(1U << 2)
#define MALI_C55_GPS_TEMPER		(1U << 3)
#define MALI_C55_GPS_SINTER_LITE	(1U << 4)
#define MALI_C55_GPS_SINTER		(1U << 5)
#define MALI_C55_GPS_IRIDIX_LTM		(1U << 6)
#define MALI_C55_GPS_IRIDIX_GTM		(1U << 7)
#define MALI_C55_GPS_CNR		(1U << 8)
#define MALI_C55_GPS_FRSCALER		(1U << 9)
#define MALI_C55_GPS_DS_PIPE		(1U << 10)

/*
 * Frames are split into zones of almost equal width and height - a zone is a
 * rectangular tile of a frame. The metering blocks within the ISP collect
 * aggregated statistics per zone. A maximum of 15x15 zones can be configured,
 * and so the statistics buffer within the hardware is sized to accommodate
 * that.
 *
 * The utilised number of zones is runtime configurable.
 */
#define MALI_C55_MAX_ZONES	(15 * 15)

/**
 * struct mali_c55_ae_1024bin_hist - Auto Exposure 1024-bin histogram statistics
 *
 * @bins:	1024 element array of 16-bit pixel counts.
 *
 * The 1024-bin histogram module collects image-global but zone-weighted
 * intensity distributions of pixels in fixed-width bins. The modules can be
 * configured into different "plane modes" which affect the contents of the
 * collected statistics. In plane mode 0, pixel intensities are taken regardless
 * of colour plane into a single 1024-bin histogram with a bin width of 4. In
 * plane mode 1, four 256-bin histograms with a bin width of 16 are collected -
 * one for each CFA colour plane. In plane modes 4, 5, 6 and 7 two 512-bin
 * histograms with a bin width of 8 are collected - in each mode one of the
 * colour planes is collected into the first histogram and all the others are
 * combined into the second. The histograms are stored consecutively in the bins
 * array.
 *
 * The 16-bit pixel counts are stored as a 4-bit exponent in the most
 * significant bits followed by a 12-bit mantissa. Conversion to a usable
 * format can be done according to the following pseudo-code::
 *
 *	if (e == 0) {
 *		bin = m * 2;
 *	} else {
 *		bin = (m + 4096) * 2^e
 *	}
 *
 * where
 *	e is the exponent value in range 0..15
 *	m is the mantissa value in range 0..4095
 *
 * The pixels used in calculating the statistics can be masked using three
 * methods:
 *
 * 1. Pixels can be skipped in X and Y directions independently.
 * 2. Minimum/Maximum intensities can be configured
 * 3. Zones can be differentially weighted, including 0 weighted to mask them
 *
 * The data for this histogram can be collected from different tap points in the
 * ISP depending on configuration - after the white balance or digital gain
 * blocks, or immediately after the input crossbar.
 */
struct mali_c55_ae_1024bin_hist {
	__u16 bins[1024];
} __attribute__((packed));

/**
 * struct mali_c55_ae_5bin_hist - Auto Exposure 5-bin histogram statistics
 *
 * @hist0:	16-bit normalised pixel count for the 0th intensity bin
 * @hist1:	16-bit normalised pixel count for the 1st intensity bin
 * @hist3:	16-bit normalised pixel count for the 3rd intensity bin
 * @hist4:	16-bit normalised pixel count for the 4th intensity bin
 *
 * The ISP generates a 5-bin histogram of normalised pixel counts within bins of
 * pixel intensity for each of 225 possible zones within a frame. The centre bin
 * of the histogram for each zone is not available from the hardware and must be
 * calculated by subtracting the values of hist0, hist1, hist3 and hist4 from
 * 0xffff as in the following equation:
 *
 *	hist2 = 0xffff - (hist0 + hist1 + hist3 + hist4)
 */
struct mali_c55_ae_5bin_hist {
	__u16 hist0;
	__u16 hist1;
	__u16 hist3;
	__u16 hist4;
} __attribute__((packed));

/**
 * struct mali_c55_awb_average_ratios - Auto White Balance colour ratios
 *
 * @avg_rg_gr:	Average R/G or G/R ratio in Q4.8 format.
 * @avg_bg_br:	Average B/G or B/R ratio in Q4.8 format.
 * @num_pixels:	The number of pixels used in the AWB calculation
 *
 * The ISP calculates and collects average colour ratios for each zone in an
 * image and stores them in Q4.8 format (the lowest 8 bits are fractional, with
 * bits [11:8] representing the integer). The exact ratios collected (either
 * R/G, B/G or G/R, B/R) are configurable through the parameters buffer. The
 * value of the 4 high bits is undefined.
 */
struct mali_c55_awb_average_ratios {
	__u16 avg_rg_gr;
	__u16 avg_bg_br;
	__u32 num_pixels;
} __attribute__((packed));

/**
 * struct mali_c55_af_statistics - Auto Focus edge and intensity statistics
 *
 * @intensity_stats:	Packed mantissa and exponent value for pixel intensity
 * @edge_stats:		Packed mantissa and exponent values for edge intensity
 *
 * The ISP collects the squared sum of pixel intensities for each zone within a
 * configurable Region of Interest on the frame. Additionally, the same data are
 * collected after being passed through a bandpass filter which removes high and
 * low frequency components - these are referred to as the edge statistics.
 *
 * The intensity and edge statistics for a zone can be used to calculate the
 * contrast information for a zone
 *
 *	C = E2 / I2
 *
 * Where I2 is the intensity statistic for a zone and E2 is the edge statistic
 * for that zone. Optimum focus is reached when C is at its maximum.
 *
 * The intensity and edge statistics are stored packed into a non-standard 16
 * bit floating point format, where the 7 most significant bits represent the
 * exponent and the 9 least significant bits the mantissa. This format can be
 * unpacked with the following pseudocode::
 *
 *	if (e == 0) {
 *		x = m;
 *	} else {
 *		x = 2^e-1 * (m + 2^9)
 *	}
 *
 * where
 *	e is the exponent value in range 0..127
 *	m is the mantissa value in range 0..511
 */
struct mali_c55_af_statistics {
	__u16 intensity_stats;
	__u16 edge_stats;
} __attribute__((packed));

/**
 * struct mali_c55_stats_buffer - 3A statistics for the mali-c55 ISP
 *
 * @ae_1024bin_hist:		1024-bin frame-global pixel intensity histogram
 * @iridix_1024bin_hist:	Post-Iridix block 1024-bin histogram
 * @ae_5bin_hists:		5-bin pixel intensity histograms for AEC
 * @reserved1:			Undefined buffer space
 * @awb_ratios:			Color balance ratios for Auto White Balance
 * @reserved2:			Undefined buffer space
 * @af_statistics:		Pixel intensity statistics for Auto Focus
 * @reserved3:			Undefined buffer space
 *
 * This struct describes the metering statistics space in the Mali-C55 ISP's
 * hardware in its entirety. The space between each defined area is marked as
 * "unknown" and may not be 0, but should not be used. The @ae_5bin_hists,
 * @awb_ratios and @af_statistics members are arrays of statistics per-zone.
 * The zones are arranged in the array in raster order starting from the top
 * left corner of the image.
 */

struct mali_c55_stats_buffer {
	struct mali_c55_ae_1024bin_hist ae_1024bin_hist;
	struct mali_c55_ae_1024bin_hist iridix_1024bin_hist;
	struct mali_c55_ae_5bin_hist ae_5bin_hists[MALI_C55_MAX_ZONES];
	__u32 reserved1[14];
	struct mali_c55_awb_average_ratios awb_ratios[MALI_C55_MAX_ZONES];
	__u32 reserved2[14];
	struct mali_c55_af_statistics af_statistics[MALI_C55_MAX_ZONES];
	__u32 reserved3[15];
} __attribute__((packed));

#endif /* __UAPI_MALI_C55_CONFIG_H */
