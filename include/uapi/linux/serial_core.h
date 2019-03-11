/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  linux/drivers/char/serial_core.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _UAPILINUX_SERIAL_CORE_H
#define _UAPILINUX_SERIAL_CORE_H

#include <linux/serial.h>

/*
 * The type definitions.  These are from Ted Ts'o's serial.h
 */
#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4
#define PORT_CIRRUS	5
#define PORT_16650	6
#define PORT_16650V2	7
#define PORT_16750	8
#define PORT_STARTECH	9
#define PORT_16C950	10
#define PORT_16654	11
#define PORT_16850	12
#define PORT_RSA	13
#define PORT_NS16550A	14
#define PORT_XSCALE	15
#define PORT_RM9000	16	/* PMC-Sierra RM9xxx internal UART */
#define PORT_OCTEON	17	/* Cavium OCTEON internal UART */
#define PORT_AR7	18	/* Texas Instruments AR7 internal UART */
#define PORT_U6_16550A	19	/* ST-Ericsson U6xxx internal UART */
#define PORT_TEGRA	20	/* NVIDIA Tegra internal UART */
#define PORT_XR17D15X	21	/* Exar XR17D15x UART */
#define PORT_LPC3220	22	/* NXP LPC32xx SoC "Standard" UART */
#define PORT_8250_CIR	23	/* CIR infrared port, has its own driver */
#define PORT_XR17V35X	24	/* Exar XR17V35x UARTs */
#define PORT_BRCM_TRUMANAGE	25
#define PORT_ALTR_16550_F32 26	/* Altera 16550 UART with 32 FIFOs */
#define PORT_ALTR_16550_F64 27	/* Altera 16550 UART with 64 FIFOs */
#define PORT_ALTR_16550_F128 28 /* Altera 16550 UART with 128 FIFOs */
#define PORT_RT2880	29	/* Ralink RT2880 internal UART */
#define PORT_16550A_FSL64 30	/* Freescale 16550 UART with 64 FIFOs */

/*
 * ARM specific type numbers.  These are not currently guaranteed
 * to be implemented, and will change in the future.  These are
 * separate so any additions to the old serial.c that occur before
 * we are merged can be easily merged here.
 */
#define PORT_PXA	31
#define PORT_AMBA	32
#define PORT_CLPS711X	33
#define PORT_SA1100	34
#define PORT_UART00	35
#define PORT_OWL	36
#define PORT_21285	37

/* Sparc type numbers.  */
#define PORT_SUNZILOG	38
#define PORT_SUNSAB	39

/* Nuvoton UART */
#define PORT_NPCM	40

/* NVIDIA Tegra Combined UART */
#define PORT_TEGRA_TCU	41

/* Intel EG20 */
#define PORT_PCH_8LINE	44
#define PORT_PCH_2LINE	45

/* DEC */
#define PORT_DZ		46
#define PORT_ZS		47

/* Parisc type numbers. */
#define PORT_MUX	48

/* Atmel AT91 SoC */
#define PORT_ATMEL	49

/* Macintosh Zilog type numbers */
#define PORT_MAC_ZILOG	50	/* m68k : not yet implemented */
#define PORT_PMAC_ZILOG	51

/* SH-SCI */
#define PORT_SCI	52
#define PORT_SCIF	53
#define PORT_IRDA	54

/* Samsung S3C2410 SoC and derivatives thereof */
#define PORT_S3C2410    55

/* SGI IP22 aka Indy / Challenge S / Indigo 2 */
#define PORT_IP22ZILOG	56

/* Sharp LH7a40x -- an ARM9 SoC series */
#define PORT_LH7A40X	57

/* PPC CPM type number */
#define PORT_CPM        58

/* MPC52xx (and MPC512x) type numbers */
#define PORT_MPC52xx	59

/* IBM icom */
#define PORT_ICOM	60

/* Samsung S3C2440 SoC */
#define PORT_S3C2440	61

/* Motorola i.MX SoC */
#define PORT_IMX	62

/* Marvell MPSC */
#define PORT_MPSC	63

/* TXX9 type number */
#define PORT_TXX9	64

/* NEC VR4100 series SIU/DSIU */
#define PORT_VR41XX_SIU		65
#define PORT_VR41XX_DSIU	66

/* Samsung S3C2400 SoC */
#define PORT_S3C2400	67

/* M32R SIO */
#define PORT_M32R_SIO	68

/*Digi jsm */
#define PORT_JSM        69

#define PORT_PNX8XXX	70

/* Hilscher netx */
#define PORT_NETX	71

/* SUN4V Hypervisor Console */
#define PORT_SUNHV	72

#define PORT_S3C2412	73

/* Xilinx uartlite */
#define PORT_UARTLITE	74

/* Blackfin bf5xx */
#define PORT_BFIN	75

/* Micrel KS8695 */
#define PORT_KS8695	76

/* Broadcom SB1250, etc. SOC */
#define PORT_SB1250_DUART	77

/* Freescale ColdFire */
#define PORT_MCF	78

/* Blackfin SPORT */
#define PORT_BFIN_SPORT		79

/* MN10300 on-chip UART numbers */
#define PORT_MN10300		80
#define PORT_MN10300_CTS	81

#define PORT_SC26XX	82

/* SH-SCI */
#define PORT_SCIFA	83

#define PORT_S3C6400	84

/* NWPSERIAL, now removed */
#define PORT_NWPSERIAL	85

/* MAX3100 */
#define PORT_MAX3100    86

/* Timberdale UART */
#define PORT_TIMBUART	87

/* Qualcomm MSM SoCs */
#define PORT_MSM	88

/* BCM63xx family SoCs */
#define PORT_BCM63XX	89

/* Aeroflex Gaisler GRLIB APBUART */
#define PORT_APBUART    90

/* Altera UARTs */
#define PORT_ALTERA_JTAGUART	91
#define PORT_ALTERA_UART	92

/* SH-SCI */
#define PORT_SCIFB	93

/* MAX310X */
#define PORT_MAX310X	94

/* TI DA8xx/66AK2x */
#define PORT_DA830	95

/* TI OMAP-UART */
#define PORT_OMAP	96

/* VIA VT8500 SoC */
#define PORT_VT8500	97

/* Cadence (Xilinx Zynq) UART */
#define PORT_XUARTPS	98

/* Atheros AR933X SoC */
#define PORT_AR933X	99

/* Energy Micro efm32 SoC */
#define PORT_EFMUART   100

/* ARC (Synopsys) on-chip UART */
#define PORT_ARC       101

/* Rocketport EXPRESS/INFINITY */
#define PORT_RP2	102

/* Freescale lpuart */
#define PORT_LPUART	103

/* SH-SCI */
#define PORT_HSCIF	104

/* ST ASC type numbers */
#define PORT_ASC       105

/* Tilera TILE-Gx UART */
#define PORT_TILEGX	106

/* MEN 16z135 UART */
#define PORT_MEN_Z135	107

/* SC16IS74xx */
#define PORT_SC16IS7XX   108

/* MESON */
#define PORT_MESON	109

/* Conexant Digicolor */
#define PORT_DIGICOLOR	110

/* SPRD SERIAL  */
#define PORT_SPRD	111

/* Cris v10 / v32 SoC */
#define PORT_CRIS	112

/* STM32 USART */
#define PORT_STM32	113

/* MVEBU UART */
#define PORT_MVEBU	114

/* Microchip PIC32 UART */
#define PORT_PIC32	115

/* MPS2 UART */
#define PORT_MPS2UART	116

/* MediaTek BTIF */
#define PORT_MTK_BTIF	117

/* RDA UART */
#define PORT_RDA	118

#endif /* _UAPILINUX_SERIAL_CORE_H */
