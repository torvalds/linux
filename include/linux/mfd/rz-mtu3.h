/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Renesas Electronics Corporation
 */
#ifndef __MFD_RZ_MTU3_H__
#define __MFD_RZ_MTU3_H__

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mutex.h>

/* 8-bit shared register offsets macros */
#define RZ_MTU3_TSTRA	0x080 /* Timer start register A */
#define RZ_MTU3_TSTRB	0x880 /* Timer start register B */

/* 16-bit shared register offset macros */
#define RZ_MTU3_TDDRA	0x016 /* Timer dead time data register A */
#define RZ_MTU3_TDDRB	0x816 /* Timer dead time data register B */
#define RZ_MTU3_TCDRA	0x014 /* Timer cycle data register A */
#define RZ_MTU3_TCDRB	0x814 /* Timer cycle data register B */
#define RZ_MTU3_TCBRA	0x022 /* Timer cycle buffer register A */
#define RZ_MTU3_TCBRB	0x822 /* Timer cycle buffer register B */
#define RZ_MTU3_TCNTSA	0x020 /* Timer subcounter A */
#define RZ_MTU3_TCNTSB	0x820 /* Timer subcounter B */

/*
 * MTU5 contains 3 timer counter registers and is totaly different
 * from other channels, so we must separate its offset
 */

/* 8-bit register offset macros of MTU3 channels except MTU5 */
#define RZ_MTU3_TIER	0 /* Timer interrupt register */
#define RZ_MTU3_NFCR	1 /* Noise filter control register */
#define RZ_MTU3_TSR	2 /* Timer status register */
#define RZ_MTU3_TCR	3 /* Timer control register */
#define RZ_MTU3_TCR2	4 /* Timer control register 2 */

/* Timer mode register 1 */
#define RZ_MTU3_TMDR1	5
#define RZ_MTU3_TMDR1_MD		GENMASK(3, 0)
#define RZ_MTU3_TMDR1_MD_NORMAL		FIELD_PREP(RZ_MTU3_TMDR1_MD, 0)
#define RZ_MTU3_TMDR1_MD_PWMMODE1	FIELD_PREP(RZ_MTU3_TMDR1_MD, 2)

#define RZ_MTU3_TIOR	6 /* Timer I/O control register */
#define RZ_MTU3_TIORH	6 /* Timer I/O control register H */
#define RZ_MTU3_TIORL	7 /* Timer I/O control register L */
/* Only MTU3/4/6/7 have TBTM registers */
#define RZ_MTU3_TBTM	8 /* Timer buffer operation transfer mode register */

/* 8-bit MTU5 register offset macros */
#define RZ_MTU3_TSTR		2 /* MTU5 Timer start register */
#define RZ_MTU3_TCNTCMPCLR	3 /* MTU5 Timer compare match clear register */
#define RZ_MTU3_TCRU		4 /* Timer control register U */
#define RZ_MTU3_TCR2U		5 /* Timer control register 2U */
#define RZ_MTU3_TIORU		6 /* Timer I/O control register U */
#define RZ_MTU3_TCRV		7 /* Timer control register V */
#define RZ_MTU3_TCR2V		8 /* Timer control register 2V */
#define RZ_MTU3_TIORV		9 /* Timer I/O control register V */
#define RZ_MTU3_TCRW		10 /* Timer control register W */
#define RZ_MTU3_TCR2W		11 /* Timer control register 2W */
#define RZ_MTU3_TIORW		12 /* Timer I/O control register W */

/* 16-bit register offset macros of MTU3 channels except MTU5 */
#define RZ_MTU3_TCNT		0 /* Timer counter */
#define RZ_MTU3_TGRA		1 /* Timer general register A */
#define RZ_MTU3_TGRB		2 /* Timer general register B */
#define RZ_MTU3_TGRC		3 /* Timer general register C */
#define RZ_MTU3_TGRD		4 /* Timer general register D */
#define RZ_MTU3_TGRE		5 /* Timer general register E */
#define RZ_MTU3_TGRF		6 /* Timer general register F */
/* Timer A/D converter start request registers */
#define RZ_MTU3_TADCR		7 /* control register */
#define RZ_MTU3_TADCORA		8 /* cycle set register A */
#define RZ_MTU3_TADCORB		9 /* cycle set register B */
#define RZ_MTU3_TADCOBRA	10 /* cycle set buffer register A */
#define RZ_MTU3_TADCOBRB	11 /* cycle set buffer register B */

/* 16-bit MTU5 register offset macros */
#define RZ_MTU3_TCNTU		0 /* MTU5 Timer counter U */
#define RZ_MTU3_TGRU		1 /* MTU5 Timer general register U */
#define RZ_MTU3_TCNTV		2 /* MTU5 Timer counter V */
#define RZ_MTU3_TGRV		3 /* MTU5 Timer general register V */
#define RZ_MTU3_TCNTW		4 /* MTU5 Timer counter W */
#define RZ_MTU3_TGRW		5 /* MTU5 Timer general register W */

/* 32-bit register offset */
#define RZ_MTU3_TCNTLW		0 /* Timer longword counter */
#define RZ_MTU3_TGRALW		1 /* Timer longword general register A */
#define RZ_MTU3_TGRBLW		2 /* Timer longowrd general register B */

#define RZ_MTU3_TMDR3		0x191 /* MTU1 Timer Mode Register 3 */

/* Macros for setting registers */
#define RZ_MTU3_TCR_CCLR	GENMASK(7, 5)
#define RZ_MTU3_TCR_CKEG	GENMASK(4, 3)
#define RZ_MTU3_TCR_TPCS	GENMASK(2, 0)
#define RZ_MTU3_TCR_CCLR_TGRA	BIT(5)
#define RZ_MTU3_TCR_CCLR_TGRC	FIELD_PREP(RZ_MTU3_TCR_CCLR, 5)
#define RZ_MTU3_TCR_CKEG_RISING	FIELD_PREP(RZ_MTU3_TCR_CKEG, 0)

#define RZ_MTU3_TIOR_IOB			GENMASK(7, 4)
#define RZ_MTU3_TIOR_IOA			GENMASK(3, 0)
#define RZ_MTU3_TIOR_OC_RETAIN			0
#define RZ_MTU3_TIOR_OC_INIT_OUT_LO_HI_OUT	2
#define RZ_MTU3_TIOR_OC_INIT_OUT_HI_TOGGLE_OUT	7

#define RZ_MTU3_TIOR_OC_IOA_H_COMP_MATCH \
	FIELD_PREP(RZ_MTU3_TIOR_IOA, RZ_MTU3_TIOR_OC_INIT_OUT_LO_HI_OUT)
#define RZ_MTU3_TIOR_OC_IOB_TOGGLE \
	FIELD_PREP(RZ_MTU3_TIOR_IOB, RZ_MTU3_TIOR_OC_INIT_OUT_HI_TOGGLE_OUT)

enum rz_mtu3_channels {
	RZ_MTU3_CHAN_0,
	RZ_MTU3_CHAN_1,
	RZ_MTU3_CHAN_2,
	RZ_MTU3_CHAN_3,
	RZ_MTU3_CHAN_4,
	RZ_MTU3_CHAN_5,
	RZ_MTU3_CHAN_6,
	RZ_MTU3_CHAN_7,
	RZ_MTU3_CHAN_8,
	RZ_MTU_NUM_CHANNELS
};

/**
 * struct rz_mtu3_channel - MTU3 channel private data
 *
 * @dev: device handle
 * @channel_number: channel number
 * @lock: Lock to protect channel state
 * @is_busy: channel state
 */
struct rz_mtu3_channel {
	struct device *dev;
	unsigned int channel_number;
	struct mutex lock;
	bool is_busy;
};

/**
 * struct rz_mtu3 - MTU3 core private data
 *
 * @clk: MTU3 module clock
 * @rz_mtu3_channel: HW channels
 * @priv_data: MTU3 core driver private data
 */
struct rz_mtu3 {
	struct clk *clk;
	struct rz_mtu3_channel channels[RZ_MTU_NUM_CHANNELS];

	void *priv_data;
};

#if IS_ENABLED(CONFIG_RZ_MTU3)
static inline bool rz_mtu3_request_channel(struct rz_mtu3_channel *ch)
{
	mutex_lock(&ch->lock);
	if (ch->is_busy) {
		mutex_unlock(&ch->lock);
		return false;
	}

	ch->is_busy = true;
	mutex_unlock(&ch->lock);

	return true;
}

static inline void rz_mtu3_release_channel(struct rz_mtu3_channel *ch)
{
	mutex_lock(&ch->lock);
	ch->is_busy = false;
	mutex_unlock(&ch->lock);
}

bool rz_mtu3_is_enabled(struct rz_mtu3_channel *ch);
void rz_mtu3_disable(struct rz_mtu3_channel *ch);
int rz_mtu3_enable(struct rz_mtu3_channel *ch);

u8 rz_mtu3_8bit_ch_read(struct rz_mtu3_channel *ch, u16 off);
u16 rz_mtu3_16bit_ch_read(struct rz_mtu3_channel *ch, u16 off);
u32 rz_mtu3_32bit_ch_read(struct rz_mtu3_channel *ch, u16 off);
u16 rz_mtu3_shared_reg_read(struct rz_mtu3_channel *ch, u16 off);

void rz_mtu3_8bit_ch_write(struct rz_mtu3_channel *ch, u16 off, u8 val);
void rz_mtu3_16bit_ch_write(struct rz_mtu3_channel *ch, u16 off, u16 val);
void rz_mtu3_32bit_ch_write(struct rz_mtu3_channel *ch, u16 off, u32 val);
void rz_mtu3_shared_reg_write(struct rz_mtu3_channel *ch, u16 off, u16 val);
void rz_mtu3_shared_reg_update_bit(struct rz_mtu3_channel *ch, u16 off,
				   u16 pos, u8 val);
#else
static inline bool rz_mtu3_request_channel(struct rz_mtu3_channel *ch)
{
	return false;
}

static inline void rz_mtu3_release_channel(struct rz_mtu3_channel *ch)
{
}

static inline bool rz_mtu3_is_enabled(struct rz_mtu3_channel *ch)
{
	return false;
}

static inline void rz_mtu3_disable(struct rz_mtu3_channel *ch)
{
}

static inline int rz_mtu3_enable(struct rz_mtu3_channel *ch)
{
	return 0;
}

static inline u8 rz_mtu3_8bit_ch_read(struct rz_mtu3_channel *ch, u16 off)
{
	return 0;
}

static inline u16 rz_mtu3_16bit_ch_read(struct rz_mtu3_channel *ch, u16 off)
{
	return 0;
}

static inline u32 rz_mtu3_32bit_ch_read(struct rz_mtu3_channel *ch, u16 off)
{
	return 0;
}

static inline u16 rz_mtu3_shared_reg_read(struct rz_mtu3_channel *ch, u16 off)
{
	return 0;
}

static inline void rz_mtu3_8bit_ch_write(struct rz_mtu3_channel *ch, u16 off, u8 val)
{
}

static inline void rz_mtu3_16bit_ch_write(struct rz_mtu3_channel *ch, u16 off, u16 val)
{
}

static inline void rz_mtu3_32bit_ch_write(struct rz_mtu3_channel *ch, u16 off, u32 val)
{
}

static inline void rz_mtu3_shared_reg_write(struct rz_mtu3_channel *ch, u16 off, u16 val)
{
}

static inline void rz_mtu3_shared_reg_update_bit(struct rz_mtu3_channel *ch,
						 u16 off, u16 pos, u8 val)
{
}
#endif

#endif /* __MFD_RZ_MTU3_H__ */
