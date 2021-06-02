/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Jonathan Neuschäfer
 *
 * Register access and version information for the Netronix embedded
 * controller.
 */

#ifndef NTXEC_H
#define NTXEC_H

#include <linux/types.h>

struct device;
struct regmap;

struct ntxec {
	struct device *dev;
	struct regmap *regmap;
};

/*
 * Some registers, such as the battery status register (0x41), are in
 * big-endian, but others only have eight significant bits, which are in the
 * first byte transmitted over I2C (the MSB of the big-endian value).
 * This convenience function converts an 8-bit value to 16-bit for use in the
 * second kind of register.
 */
static inline __be16 ntxec_reg8(u8 value)
{
	return value << 8;
}

/* Known firmware versions */
#define NTXEC_VERSION_KOBO_AURA	0xd726	/* found in Kobo Aura */
#define NTXEC_VERSION_TOLINO_SHINE2 0xf110 /* found in Tolino Shine 2 HD */

#endif
