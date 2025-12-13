/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Microchip SAMA7 SFRBU registers offsets and bit definitions.
 *
 * Copyright (C) [2020] Microchip Technology Inc. and its subsidiaries
 *
 * Author: Claudu Beznea <claudiu.beznea@microchip.com>
 */

#ifndef __SAMA7_SFRBU_H__
#define __SAMA7_SFRBU_H__

#ifdef CONFIG_SOC_SAMA7

#define AT91_SFRBU_PSWBU			(0x00)		/* SFRBU Power Switch BU Control Register */
#define		AT91_SFRBU_PSWBU_PSWKEY		(0x4BD20C << 8)	/* Specific value mandatory to allow writing of other register bits */
#define		AT91_SFRBU_PSWBU_STATE		(1 << 2)	/* Power switch BU state */
#define		AT91_SFRBU_PSWBU_SOFTSWITCH	(1 << 1)	/* Power switch BU source selection */
#define		AT91_SFRBU_PSWBU_CTRL		(1 << 0)	/* Power switch BU control */

#define AT91_FRBU_DDRPWR			(0x10)		/* SFRBU DDR Power Control Register */
#define		AT91_FRBU_DDRPWR_STATE		(1 << 0)	/* DDR Power Mode State */

#endif /* CONFIG_SOC_SAMA7 */

#endif /* __SAMA7_SFRBU_H__ */

