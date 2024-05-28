/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __DRM_ELD_H__
#define __DRM_ELD_H__

#include <linux/types.h>

struct cea_sad;

/* ELD Header Block */
#define DRM_ELD_HEADER_BLOCK_SIZE	4

#define DRM_ELD_VER			0
# define DRM_ELD_VER_SHIFT		3
# define DRM_ELD_VER_MASK		(0x1f << 3)
# define DRM_ELD_VER_CEA861D		(2 << 3) /* supports 861D or below */
# define DRM_ELD_VER_CANNED		(0x1f << 3)

#define DRM_ELD_BASELINE_ELD_LEN	2	/* in dwords! */

/* ELD Baseline Block for ELD_Ver == 2 */
#define DRM_ELD_CEA_EDID_VER_MNL	4
# define DRM_ELD_CEA_EDID_VER_SHIFT	5
# define DRM_ELD_CEA_EDID_VER_MASK	(7 << 5)
# define DRM_ELD_CEA_EDID_VER_NONE	(0 << 5)
# define DRM_ELD_CEA_EDID_VER_CEA861	(1 << 5)
# define DRM_ELD_CEA_EDID_VER_CEA861A	(2 << 5)
# define DRM_ELD_CEA_EDID_VER_CEA861BCD	(3 << 5)
# define DRM_ELD_MNL_SHIFT		0
# define DRM_ELD_MNL_MASK		(0x1f << 0)

#define DRM_ELD_SAD_COUNT_CONN_TYPE	5
# define DRM_ELD_SAD_COUNT_SHIFT	4
# define DRM_ELD_SAD_COUNT_MASK		(0xf << 4)
# define DRM_ELD_CONN_TYPE_SHIFT	2
# define DRM_ELD_CONN_TYPE_MASK		(3 << 2)
# define DRM_ELD_CONN_TYPE_HDMI		(0 << 2)
# define DRM_ELD_CONN_TYPE_DP		(1 << 2)
# define DRM_ELD_SUPPORTS_AI		(1 << 1)
# define DRM_ELD_SUPPORTS_HDCP		(1 << 0)

#define DRM_ELD_AUD_SYNCH_DELAY		6	/* in units of 2 ms */
# define DRM_ELD_AUD_SYNCH_DELAY_MAX	0xfa	/* 500 ms */

#define DRM_ELD_SPEAKER			7
# define DRM_ELD_SPEAKER_MASK		0x7f
# define DRM_ELD_SPEAKER_RLRC		(1 << 6)
# define DRM_ELD_SPEAKER_FLRC		(1 << 5)
# define DRM_ELD_SPEAKER_RC		(1 << 4)
# define DRM_ELD_SPEAKER_RLR		(1 << 3)
# define DRM_ELD_SPEAKER_FC		(1 << 2)
# define DRM_ELD_SPEAKER_LFE		(1 << 1)
# define DRM_ELD_SPEAKER_FLR		(1 << 0)

#define DRM_ELD_PORT_ID			8	/* offsets 8..15 inclusive */
# define DRM_ELD_PORT_ID_LEN		8

#define DRM_ELD_MANUFACTURER_NAME0	16
#define DRM_ELD_MANUFACTURER_NAME1	17

#define DRM_ELD_PRODUCT_CODE0		18
#define DRM_ELD_PRODUCT_CODE1		19

#define DRM_ELD_MONITOR_NAME_STRING	20	/* offsets 20..(20+mnl-1) inclusive */

#define DRM_ELD_CEA_SAD(mnl, sad)	(20 + (mnl) + 3 * (sad))

/**
 * drm_eld_mnl - Get ELD monitor name length in bytes.
 * @eld: pointer to an eld memory structure with mnl set
 */
static inline int drm_eld_mnl(const u8 *eld)
{
	return (eld[DRM_ELD_CEA_EDID_VER_MNL] & DRM_ELD_MNL_MASK) >> DRM_ELD_MNL_SHIFT;
}

int drm_eld_sad_get(const u8 *eld, int sad_index, struct cea_sad *cta_sad);
int drm_eld_sad_set(u8 *eld, int sad_index, const struct cea_sad *cta_sad);

/**
 * drm_eld_sad - Get ELD SAD structures.
 * @eld: pointer to an eld memory structure with sad_count set
 */
static inline const u8 *drm_eld_sad(const u8 *eld)
{
	unsigned int ver, mnl;

	ver = (eld[DRM_ELD_VER] & DRM_ELD_VER_MASK) >> DRM_ELD_VER_SHIFT;
	if (ver != 2 && ver != 31)
		return NULL;

	mnl = drm_eld_mnl(eld);
	if (mnl > 16)
		return NULL;

	return eld + DRM_ELD_CEA_SAD(mnl, 0);
}

/**
 * drm_eld_sad_count - Get ELD SAD count.
 * @eld: pointer to an eld memory structure with sad_count set
 */
static inline int drm_eld_sad_count(const u8 *eld)
{
	return (eld[DRM_ELD_SAD_COUNT_CONN_TYPE] & DRM_ELD_SAD_COUNT_MASK) >>
		DRM_ELD_SAD_COUNT_SHIFT;
}

/**
 * drm_eld_calc_baseline_block_size - Calculate baseline block size in bytes
 * @eld: pointer to an eld memory structure with mnl and sad_count set
 *
 * This is a helper for determining the payload size of the baseline block, in
 * bytes, for e.g. setting the Baseline_ELD_Len field in the ELD header block.
 */
static inline int drm_eld_calc_baseline_block_size(const u8 *eld)
{
	return DRM_ELD_MONITOR_NAME_STRING - DRM_ELD_HEADER_BLOCK_SIZE +
		drm_eld_mnl(eld) + drm_eld_sad_count(eld) * 3;
}

/**
 * drm_eld_size - Get ELD size in bytes
 * @eld: pointer to a complete eld memory structure
 *
 * The returned value does not include the vendor block. It's vendor specific,
 * and comprises of the remaining bytes in the ELD memory buffer after
 * drm_eld_size() bytes of header and baseline block.
 *
 * The returned value is guaranteed to be a multiple of 4.
 */
static inline int drm_eld_size(const u8 *eld)
{
	return DRM_ELD_HEADER_BLOCK_SIZE + eld[DRM_ELD_BASELINE_ELD_LEN] * 4;
}

/**
 * drm_eld_get_spk_alloc - Get speaker allocation
 * @eld: pointer to an ELD memory structure
 *
 * The returned value is the speakers mask. User has to use %DRM_ELD_SPEAKER
 * field definitions to identify speakers.
 */
static inline u8 drm_eld_get_spk_alloc(const u8 *eld)
{
	return eld[DRM_ELD_SPEAKER] & DRM_ELD_SPEAKER_MASK;
}

/**
 * drm_eld_get_conn_type - Get device type hdmi/dp connected
 * @eld: pointer to an ELD memory structure
 *
 * The caller need to use %DRM_ELD_CONN_TYPE_HDMI or %DRM_ELD_CONN_TYPE_DP to
 * identify the display type connected.
 */
static inline u8 drm_eld_get_conn_type(const u8 *eld)
{
	return eld[DRM_ELD_SAD_COUNT_CONN_TYPE] & DRM_ELD_CONN_TYPE_MASK;
}

#endif /* __DRM_ELD_H__ */
