/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 */
#ifndef _UAPILINUX_SERIAL_CORE_H
#define _UAPILINUX_SERIAL_CORE_H

#include <linux/serial.h>

/*
 * The type definitions.  These are from Ted Ts'o's serial.h
 * By historical reasons the values from 0 to 13 are defined
 * in the include/uapi/linux/serial.h, do not define them here.
 * Values 0 to 19 are used by setserial from busybox and must never
 * be modified.
 */
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

/* ASPEED AST2x00 virtual UART */
#define PORT_ASPEED_VUART	42

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

/* SGI IP22 aka Indy / Challenge S / Indigo 2 */
#define PORT_IP22ZILOG	56

/* PPC CPM type number */
#define PORT_CPM        58

/* MPC52xx (and MPC512x) type numbers */
#define PORT_MPC52xx	59

/* IBM icom */
#define PORT_ICOM	60

/* Motorola i.MX SoC */
#define PORT_IMX	62

/* TXX9 type number */
#define PORT_TXX9	64

/*Digi jsm */
#define PORT_JSM        69

/* SUN4V Hypervisor Console */
#define PORT_SUNHV	72

/* Xilinx uartlite */
#define PORT_UARTLITE	74

/* Broadcom BCM7271 UART */
#define PORT_BCM7271	76

/* Broadcom SB1250, etc. SOC */
#define PORT_SB1250_DUART	77

/* Freescale ColdFire */
#define PORT_MCF	78

#define PORT_SC26XX	82

/* SH-SCI */
#define PORT_SCIFA	83

#define PORT_S3C6400	84

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

/* MCHP 16550A UART with 256 byte FIFOs */
#define PORT_MCHP16550A	100

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

/* MEN 16z135 UART */
#define PORT_MEN_Z135	107

/* SC16IS7xx */
#define PORT_SC16IS7XX   108

/* MESON */
#define PORT_MESON	109

/* Conexant Digicolor */
#define PORT_DIGICOLOR	110

/* SPRD SERIAL  */
#define PORT_SPRD	111

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

/* Socionext Milbeaut UART */
#define PORT_MLB_USIO	119

/* SiFive UART */
#define PORT_SIFIVE_V0	120

/* Sunix UART */
#define PORT_SUNIX	121

/* Freescale LINFlexD UART */
#define PORT_LINFLEXUART	122

/* Sunplus UART */
#define PORT_SUNPLUS	123

/* Generic type identifier for ports which type is not important to userspace. */
#define PORT_GENERIC	(-1)

#endif /* _UAPILINUX_SERIAL_CORE_H */
