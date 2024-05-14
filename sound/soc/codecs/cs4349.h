/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ALSA SoC CS4349 codec driver
 *
 * Copyright 2015 Cirrus Logic, Inc.
 *
 * Author: Tim Howe <Tim.Howe@cirrus.com>
 */

#ifndef __CS4349_H__
#define __CS4349_H__

/* CS4349 registers addresses */
#define CS4349_CHIPID		0x01	/* Device and Rev ID, Read Only */
#define CS4349_MODE		0x02	/* Mode Control */
#define CS4349_VMI		0x03	/* Volume, Mixing, Inversion Control */
#define CS4349_MUTE		0x04	/* Mute Control */
#define CS4349_VOLA		0x05	/* DAC Channel A Volume Control */
#define CS4349_VOLB		0x06	/* DAC Channel B Volume Control */
#define CS4349_RMPFLT		0x07	/* Ramp and Filter Control */
#define CS4349_MISC		0x08	/* Power Down,Freeze Control,Pop Stop*/

#define CS4349_I2C_INCR		0x80


/* Device and Revision ID */
#define CS4349_REVA		0xF0	/* Rev A */
#define CS4349_REVB		0xF1	/* Rev B */
#define CS4349_REVC2		0xFF	/* Rev C2 */


/* PDN_DONE Poll Maximum
 * If soft ramp is set it will take much longer to power down
 * the system.
 */
#define PDN_POLL_MAX		900


/* Bitfield Definitions */

/* CS4349_MODE */
/* (Digital Interface Format, De-Emphasis Control, Functional Mode */
#define DIF2			(1 << 6)
#define DIF1			(1 << 5)
#define DIF0			(1 << 4)
#define DEM1			(1 << 3)
#define DEM0			(1 << 2)
#define FM1			(1 << 1)
#define DIF_LEFT_JST		0x00
#define DIF_I2S			0x01
#define DIF_RGHT_JST16		0x02
#define DIF_RGHT_JST24		0x03
#define DIF_TDM0		0x04
#define DIF_TDM1		0x05
#define DIF_TDM2		0x06
#define DIF_TDM3		0x07
#define DIF_MASK		0x70
#define MODE_FORMAT(x)		(((x)&7)<<4)
#define DEM_MASK		0x0C
#define NO_DEM			0x00
#define DEM_441			0x04
#define DEM_48K			0x08
#define DEM_32K			0x0C
#define FM_AUTO			0x00
#define FM_SNGL			0x01
#define FM_DBL			0x02
#define FM_QUAD			0x03
#define FM_SNGL_MIN		30000
#define FM_SNGL_MAX		54000
#define FM_DBL_MAX		108000
#define FM_QUAD_MAX		216000
#define FM_MASK			0x03

/* CS4349_VMI (VMI = Volume, Mixing and Inversion Controls) */
#define VOLBISA			(1 << 7)
#define VOLAISB			(1 << 7)
/* INVERT_A only available for Left Jstfd, Right Jstfd16 and Right Jstfd24 */
#define INVERT_A		(1 << 6)
/* INVERT_B only available for Left Jstfd, Right Jstfd16 and Right Jstfd24 */
#define INVERT_B		(1 << 5)
#define ATAPI3			(1 << 3)
#define ATAPI2			(1 << 2)
#define ATAPI1			(1 << 1)
#define ATAPI0			(1 << 0)
#define MUTEAB			0x00
#define MUTEA_RIGHTB		0x01
#define MUTEA_LEFTB		0x02
#define MUTEA_SUMLRDIV2B	0x03
#define RIGHTA_MUTEB		0x04
#define RIGHTA_RIGHTB		0x05
#define RIGHTA_LEFTB		0x06
#define RIGHTA_SUMLRDIV2B	0x07
#define LEFTA_MUTEB		0x08
#define LEFTA_RIGHTB		0x09	/* Default */
#define LEFTA_LEFTB		0x0A
#define LEFTA_SUMLRDIV2B	0x0B
#define SUMLRDIV2A_MUTEB	0x0C
#define SUMLRDIV2A_RIGHTB	0x0D
#define SUMLRDIV2A_LEFTB	0x0E
#define SUMLRDIV2_AB		0x0F
#define CHMIX_MASK		0x0F

/* CS4349_MUTE */
#define AUTOMUTE		(1 << 7)
#define MUTEC_AB		(1 << 5)
#define MUTE_A			(1 << 4)
#define MUTE_B			(1 << 3)
#define MUTE_AB_MASK		0x18

/* CS4349_RMPFLT (Ramp and Filter Control) */
#define SCZ1			(1 << 7)
#define SCZ0			(1 << 6)
#define RMP_UP			(1 << 5)
#define RMP_DN			(1 << 4)
#define FILT_SEL		(1 << 2)
#define IMMDT_CHNG		0x31
#define ZEROCRSS		0x71
#define SOFT_RMP		0xB1
#define SFTRMP_ZEROCRSS		0xF1
#define SR_ZC_MASK		0xC0

/* CS4349_MISC */
#define PWR_DWN			(1 << 7)
#define FREEZE			(1 << 5)
#define POPG_EN			(1 << 4)

#endif	/* __CS4349_H__ */
