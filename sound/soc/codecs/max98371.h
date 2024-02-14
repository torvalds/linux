/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * max98371.h -- MAX98371 ALSA SoC Audio driver
 *
 * Copyright 2011-2012 Maxim Integrated Products
 */

#ifndef _MAX98371_H
#define _MAX98371_H

#define MAX98371_IRQ_CLEAR1			0x01
#define MAX98371_IRQ_CLEAR2			0x02
#define MAX98371_IRQ_CLEAR3			0x03
#define MAX98371_DAI_CLK			0x10
#define MAX98371_DAI_BSEL_MASK			0xF
#define MAX98371_DAI_BSEL_32			2
#define MAX98371_DAI_BSEL_48			3
#define MAX98371_DAI_BSEL_64			4
#define MAX98371_SPK_SR				0x11
#define MAX98371_SPK_SR_MASK			0xF
#define MAX98371_SPK_SR_32			6
#define MAX98371_SPK_SR_44			7
#define MAX98371_SPK_SR_48			8
#define MAX98371_SPK_SR_88			10
#define MAX98371_SPK_SR_96			11
#define MAX98371_DAI_CHANNEL			0x15
#define MAX98371_CHANNEL_MASK			0x3
#define MAX98371_MONOMIX_SRC			0x18
#define MAX98371_MONOMIX_CFG			0x19
#define MAX98371_HPF				0x1C
#define MAX98371_MONOMIX_SRC_MASK		0xFF
#define MONOMIX_RX_0_1				((0x1)<<(4))
#define M98371_DAI_CHANNEL_I2S			0x3
#define MAX98371_DIGITAL_GAIN			0x2D
#define MAX98371_DIGITAL_GAIN_WIDTH		0x7
#define MAX98371_GAIN				0x2E
#define MAX98371_GAIN_SHIFT			0x4
#define MAX98371_GAIN_WIDTH			0x4
#define MAX98371_DHT_MAX_WIDTH			4
#define MAX98371_FMT				0x14
#define MAX98371_CHANSZ_WIDTH			6
#define MAX98371_FMT_MASK		        ((0x3)<<(MAX98371_CHANSZ_WIDTH))
#define MAX98371_FMT_MODE_MASK		        ((0x7)<<(3))
#define MAX98371_DAI_LEFT		        ((0x1)<<(3))
#define MAX98371_DAI_RIGHT		        ((0x2)<<(3))
#define MAX98371_DAI_CHANSZ_16                  ((1)<<(MAX98371_CHANSZ_WIDTH))
#define MAX98371_DAI_CHANSZ_24                  ((2)<<(MAX98371_CHANSZ_WIDTH))
#define MAX98371_DAI_CHANSZ_32                  ((3)<<(MAX98371_CHANSZ_WIDTH))
#define MAX98371_DHT  0x32
#define MAX98371_DHT_STEP			0x3
#define MAX98371_DHT_GAIN			0x31
#define MAX98371_DHT_GAIN_WIDTH			0x4
#define MAX98371_DHT_ROT_WIDTH			0x4
#define MAX98371_SPK_ENABLE			0x4A
#define MAX98371_GLOBAL_ENABLE			0x50
#define MAX98371_SOFT_RESET			0x51
#define MAX98371_VERSION			0xFF


struct max98371_priv {
	struct regmap *regmap;
};
#endif
