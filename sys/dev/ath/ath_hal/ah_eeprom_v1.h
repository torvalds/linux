/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */
#ifndef _ATH_AH_EEPROM_V1_H_
#define _ATH_AH_EEPROM_V1_H_

#include "ah_eeprom.h"

/*
 * EEPROM defines for Version 1 Crete EEPROM.
 *
 * The EEPROM is segmented into three sections:
 *
 *    PCI/Cardbus default configuration settings
 *    Cardbus CIS tuples and vendor-specific data
 *    Atheros-specific data
 *
 * EEPROM entries are read 32-bits at a time through the PCI bus
 * interface but are all 16-bit values.
 *
 * Access to the Atheros-specific data is controlled by protection
 * bits and the data is checksum'd.  The driver reads the Atheros
 * data from the EEPROM at attach and caches it in its private state.
 * This data includes the local regulatory domain, channel calibration
 * settings, and phy-related configuration settings.
 */
#define	AR_EEPROM_MAC(i)	(0x1f-(i))/* MAC address word */
#define	AR_EEPROM_MAGIC		0x3d	/* magic number */
#define AR_EEPROM_PROTECT	0x3f	/* Atheros segment protect register */
#define	AR_EEPROM_PROTOTECT_WP_128_191	0x80
#define AR_EEPROM_REG_DOMAIN	0xbf	/* Current regulatory domain register */
#define AR_EEPROM_ATHEROS_BASE	0xc0	/* Base of Atheros-specific data */
#define AR_EEPROM_ATHEROS_MAX	64	/* 64x2=128 bytes of EEPROM settings */
#define	AR_EEPROM_ATHEROS(n)	(AR_EEPROM_ATHEROS_BASE+(n))
#define	AR_EEPROM_VERSION	AR_EEPROM_ATHEROS(1)
#define AR_EEPROM_ATHEROS_TP_SETTINGS	0x09	/* Transmit power settings */
#define AR_REG_DOMAINS_MAX	4	/* # of Regulatory Domains */
#define AR_CHANNELS_MAX		5	/* # of Channel calibration groups */
#define AR_TP_SETTINGS_SIZE	11	/* # locations/Channel group */
#define AR_TP_SCALING_ENTRIES	11	/* # entries in transmit power dBm->pcdac */

/*
 * NB: we store the rfsilent select+polarity data packed
 *     with the encoding used in later parts so values
 *     returned to applications are consistent.
 */
#define AR_EEPROM_RFSILENT_GPIO_SEL	0x001c
#define AR_EEPROM_RFSILENT_GPIO_SEL_S	2
#define AR_EEPROM_RFSILENT_POLARITY	0x0002
#define AR_EEPROM_RFSILENT_POLARITY_S	1

#define AR_I2DBM(x)	((uint8_t)((x * 2) + 3))

/*
 * Transmit power and channel calibration settings.
 */
struct tpcMap {
	uint8_t		pcdac[AR_TP_SCALING_ENTRIES];
	uint8_t		gainF[AR_TP_SCALING_ENTRIES];
	uint8_t		rate36;
	uint8_t		rate48;
	uint8_t		rate54;
	uint8_t		regdmn[AR_REG_DOMAINS_MAX];
};

/*
 * Information retrieved from EEPROM.
 */
typedef struct {
	uint16_t	ee_version;		/* Version field */
	uint16_t	ee_protect;		/* EEPROM protect field */
	uint16_t	ee_antenna;		/* Antenna Settings */
	uint16_t	ee_biasCurrents;	/* OB, DB */
	uint8_t		ee_thresh62;		/* thresh62 */
	uint8_t		ee_xlnaOn;		/* External LNA timing */
	uint8_t		ee_xpaOff;		/* Extern output stage timing */
	uint8_t		ee_xpaOn;		/* Extern output stage timing */
	uint8_t		ee_rfKill;		/* Single low bit signalling if RF Kill is implemented */
	uint8_t		ee_devType;		/* Type: PCI, miniPCI, CB */
	uint8_t		ee_regDomain[AR_REG_DOMAINS_MAX];
						/* calibrated reg domains */
	struct tpcMap	ee_tpc[AR_CHANNELS_MAX];
} HAL_EEPROM_v1;
#endif /* _ATH_AH_EEPROM_V1_H_ */
