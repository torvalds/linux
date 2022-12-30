/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Microchip SAMA7 UDDR Controller and DDR3 PHY Controller registers offsets
 * and bit definitions.
 *
 * Copyright (C) [2020] Microchip Technology Inc. and its subsidiaries
 *
 * Author: Claudu Beznea <claudiu.beznea@microchip.com>
 */

#ifndef __SAMA7_DDR_H__
#define __SAMA7_DDR_H__

/* DDR3PHY */
#define DDR3PHY_PIR				(0x04)		/* DDR3PHY PHY Initialization Register	*/
#define	DDR3PHY_PIR_DLLBYP			(1 << 17)	/* DLL Bypass */
#define		DDR3PHY_PIR_ITMSRST		(1 << 4)	/* Interface Timing Module Soft Reset */
#define	DDR3PHY_PIR_DLLLOCK			(1 << 2)	/* DLL Lock */
#define		DDR3PHY_PIR_DLLSRST		(1 << 1)	/* DLL Soft Rest */
#define	DDR3PHY_PIR_INIT			(1 << 0)	/* Initialization Trigger */

#define DDR3PHY_PGCR				(0x08)		/* DDR3PHY PHY General Configuration Register */
#define		DDR3PHY_PGCR_CKDV1		(1 << 13)	/* CK# Disable Value */
#define		DDR3PHY_PGCR_CKDV0		(1 << 12)	/* CK Disable Value */

#define	DDR3PHY_PGSR				(0x0C)		/* DDR3PHY PHY General Status Register */
#define		DDR3PHY_PGSR_IDONE		(1 << 0)	/* Initialization Done */

#define	DDR3PHY_ACDLLCR				(0x14)		/* DDR3PHY AC DLL Control Register */
#define		DDR3PHY_ACDLLCR_DLLSRST		(1 << 30)	/* DLL Soft Reset */

#define DDR3PHY_ACIOCR				(0x24)		/* DDR3PHY AC I/O Configuration Register */
#define		DDR3PHY_ACIOCR_CSPDD_CS0	(1 << 18)	/* CS#[0] Power Down Driver */
#define		DDR3PHY_ACIOCR_CKPDD_CK0	(1 << 8)	/* CK[0] Power Down Driver */
#define		DDR3PHY_ACIORC_ACPDD		(1 << 3)	/* AC Power Down Driver */

#define DDR3PHY_DXCCR				(0x28)		/* DDR3PHY DATX8 Common Configuration Register */
#define		DDR3PHY_DXCCR_DXPDR		(1 << 3)	/* Data Power Down Receiver */

#define DDR3PHY_DSGCR				(0x2C)		/* DDR3PHY DDR System General Configuration Register */
#define		DDR3PHY_DSGCR_ODTPDD_ODT0	(1 << 20)	/* ODT[0] Power Down Driver */

#define DDR3PHY_ZQ0SR0				(0x188)		/* ZQ status register 0 */
#define DDR3PHY_ZQ0SR0_PDO_OFF			(0)		/* Pull-down output impedance select offset */
#define DDR3PHY_ZQ0SR0_PUO_OFF			(5)		/* Pull-up output impedance select offset */
#define DDR3PHY_ZQ0SR0_PDODT_OFF		(10)		/* Pull-down on-die termination impedance select offset */
#define DDR3PHY_ZQ0SRO_PUODT_OFF		(15)		/* Pull-up on-die termination impedance select offset */

#define	DDR3PHY_DX0DLLCR			(0x1CC)		/* DDR3PHY DATX8 DLL Control Register */
#define	DDR3PHY_DX1DLLCR			(0x20C)		/* DDR3PHY DATX8 DLL Control Register */
#define		DDR3PHY_DXDLLCR_DLLDIS		(1 << 31)	/* DLL Disable */

/* UDDRC */
#define UDDRC_STAT				(0x04)		/* UDDRC Operating Mode Status Register */
#define		UDDRC_STAT_SELFREF_TYPE_DIS	(0x0 << 4)	/* SDRAM is not in Self-refresh */
#define		UDDRC_STAT_SELFREF_TYPE_PHY	(0x1 << 4)	/* SDRAM is in Self-refresh, which was caused by PHY Master Request */
#define		UDDRC_STAT_SELFREF_TYPE_SW	(0x2 << 4)	/* SDRAM is in Self-refresh, which was not caused solely under Automatic Self-refresh control */
#define		UDDRC_STAT_SELFREF_TYPE_AUTO	(0x3 << 4)	/* SDRAM is in Self-refresh, which was caused by Automatic Self-refresh only */
#define		UDDRC_STAT_SELFREF_TYPE_MSK	(0x3 << 4)	/* Self-refresh type mask */
#define		UDDRC_STAT_OPMODE_INIT		(0x0 << 0)	/* Init */
#define		UDDRC_STAT_OPMODE_NORMAL	(0x1 << 0)	/* Normal */
#define		UDDRC_STAT_OPMODE_PWRDOWN	(0x2 << 0)	/* Power-down */
#define		UDDRC_STAT_OPMODE_SELF_REFRESH	(0x3 << 0)	/* Self-refresh */
#define		UDDRC_STAT_OPMODE_MSK		(0x7 << 0)	/* Operating mode mask */

#define UDDRC_PWRCTL				(0x30)		/* UDDRC Low Power Control Register */
#define		UDDRC_PWRCTL_SELFREF_EN		(1 << 0)	/* Automatic self-refresh */
#define		UDDRC_PWRCTL_SELFREF_SW		(1 << 5)	/* Software self-refresh */

#define UDDRC_DFIMISC				(0x1B0)		/* UDDRC DFI Miscellaneous Control Register */
#define		UDDRC_DFIMISC_DFI_INIT_COMPLETE_EN (1 << 0)	/* PHY initialization complete enable signal */

#define UDDRC_SWCTRL				(0x320)		/* UDDRC Software Register Programming Control Enable */
#define		UDDRC_SWCTRL_SW_DONE		(1 << 0)	/* Enable quasi-dynamic register programming outside reset */

#define UDDRC_SWSTAT				(0x324)		/* UDDRC Software Register Programming Control Status */
#define		UDDRC_SWSTAT_SW_DONE_ACK	(1 << 0)	/* Register programming done */

#define UDDRC_PSTAT				(0x3FC)		/* UDDRC Port Status Register */
#define	UDDRC_PSTAT_ALL_PORTS			(0x1F001F)	/* Read + writes outstanding transactions on all ports */

#define UDDRC_PCTRL_0				(0x490)		/* UDDRC Port 0 Control Register */
#define UDDRC_PCTRL_1				(0x540)		/* UDDRC Port 1 Control Register */
#define UDDRC_PCTRL_2				(0x5F0)		/* UDDRC Port 2 Control Register */
#define UDDRC_PCTRL_3				(0x6A0)		/* UDDRC Port 3 Control Register */
#define UDDRC_PCTRL_4				(0x750)		/* UDDRC Port 4 Control Register */

#endif /* __SAMA7_DDR_H__ */
