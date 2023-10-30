/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2011-2012, 2014, 2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/skbuff.h>

#ifndef _BAM_DMUX_H
#define _BAM_DMUX_H

#define BAM_DMUX_CH_NAME_MAX_LEN	20

enum {
	BAM_DMUX_DATA_RMNET_0,
	BAM_DMUX_DATA_RMNET_1,
	BAM_DMUX_DATA_RMNET_2,
	BAM_DMUX_DATA_RMNET_3,
	BAM_DMUX_DATA_RMNET_4,
	BAM_DMUX_DATA_RMNET_5,
	BAM_DMUX_DATA_RMNET_6,
	BAM_DMUX_DATA_RMNET_7,
	BAM_DMUX_USB_RMNET_0,
	BAM_DMUX_RESERVED_0, /* 9..11 are reserved*/
	BAM_DMUX_RESERVED_1,
	BAM_DMUX_RESERVED_2,
	BAM_DMUX_DATA_REV_RMNET_0,
	BAM_DMUX_DATA_REV_RMNET_1,
	BAM_DMUX_DATA_REV_RMNET_2,
	BAM_DMUX_DATA_REV_RMNET_3,
	BAM_DMUX_DATA_REV_RMNET_4,
	BAM_DMUX_DATA_REV_RMNET_5,
	BAM_DMUX_DATA_REV_RMNET_6,
	BAM_DMUX_DATA_REV_RMNET_7,
	BAM_DMUX_DATA_REV_RMNET_8,
	BAM_DMUX_USB_DPL,
	BAM_DMUX_NUM_CHANNELS
};

/* event type enum */
enum {
	BAM_DMUX_RECEIVE, /* data is struct sk_buff */
	BAM_DMUX_WRITE_DONE, /* data is struct sk_buff */
	BAM_DMUX_UL_CONNECTED, /* data is null */
	BAM_DMUX_UL_DISCONNECTED, /*data is null */
	BAM_DMUX_TRANSMIT_SIZE, /* data is maximum negotiated transmit MTU */
};

/*
 * Open a bam_dmux logical channel
 *     id - the logical channel to open
 *     priv - private data pointer to be passed to the notify callback
 *     notify - event callback function
 *          priv - private data pointer passed to msm_bam_dmux_open()
 *          event_type - type of event
 *          data - data relevant to event.  May not be valid. See event_type
 *                    enum for valid cases.
 */
#ifdef CONFIG_MSM_BAM_DMUX
int msm_bam_dmux_open(uint32_t id, void *priv,
		       void (*notify)(void *priv, int event_type,
						unsigned long data));

int msm_bam_dmux_close(uint32_t id);

int msm_bam_dmux_write(uint32_t id, struct sk_buff *skb);

int msm_bam_dmux_kickoff_ul_wakeup(void);

int msm_bam_dmux_ul_power_vote(void);

int msm_bam_dmux_ul_power_unvote(void);

int msm_bam_dmux_is_ch_full(uint32_t id);

int msm_bam_dmux_is_ch_low(uint32_t id);

int msm_bam_dmux_reg_notify(void *priv,
		       void (*notify)(void *priv, int event_type,
						unsigned long data));
#else
static inline int msm_bam_dmux_open(uint32_t id, void *priv,
		       void (*notify)(void *priv, int event_type,
						unsigned long data))
{
	return -ENODEV;
}

static inline int msm_bam_dmux_close(uint32_t id)
{
	return -ENODEV;
}

static inline int msm_bam_dmux_write(uint32_t id, struct sk_buff *skb)
{
	return -ENODEV;
}

static inline int msm_bam_dmux_kickoff_ul_wakeup(void)
{
	return -ENODEV;
}

static inline int msm_bam_dmux_ul_power_vote(void)
{
	return -ENODEV;
}

static inline int msm_bam_dmux_ul_power_unvote(void)
{
	return -ENODEV;
}

static inline int msm_bam_dmux_is_ch_full(uint32_t id)
{
	return -ENODEV;
}

static inline int msm_bam_dmux_is_ch_low(uint32_t id)
{
	return -ENODEV;
}

static inline int msm_bam_dmux_reg_notify(void *priv,
		       void (*notify)(void *priv, int event_type,
						unsigned long data))
{
	return -ENODEV;
}
#endif
#endif /* _BAM_DMUX_H */
