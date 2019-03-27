/*-
 * Copyright (c) 2016 Andriy Gapon <avg@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * The following registers, bits and magic values are defined in Register
 * Reference Guide documents for SB600, SB7x0, SB8x0, SB9x0 southbridges and
 * various versions of Fusion Controller Hubs (FCHs).  FCHs integrated into
 * CPUs are documented in BIOS and Kernel Development Guide documents for
 * the corresponding processor families.
 *
 * At present there are three classes of supported chipsets:
 * - SB600 and S7x0 southbridges where the SMBus controller device has
 *   a PCI Device ID of 0x43851002 and a revision less than 0x40
 * - several types of southbridges and FCHs:
 *   o SB8x0, SB9x0 southbridges where the SMBus controller device has a PCI
 *     Device ID of 0x43851002 and a revision greater than or equal to 0x40
 *   o FCHs where the controller has an ID of 0x780b1022 and a revision less
 *     than 0x41 (various variants of "Hudson" and "Bolton" as well as FCHs
 *     integrated into processors, e.g. "Kabini")
 *   o FCHs where the controller has an ID of 0x790b1022 and a revision less
 *     than 0x49
 * - several types of FCHs:
 *   o FCHs where the SMBus controller device has a PCI Device ID of 0x780b1022
 *     and a revision greater than or equal to 0x41 (integrated into "Mullins"
 *     processors, code named "ML")
 *   o FCHs where the controller has an ID of 0x790b1022 and a revision greater
 *     than or equal to 0x49 (integrated into "Carrizo" processors, code named
 *     "KERNCZ" or "CZ")
 *
 * The register definitions are compatible within the classes and may be
 * incompatible accross them.
 */

/*
 * IO registers for accessing the PMIO space.
 * See SB7xx RRG 2.3.3.1.1, for instance.
 */
#define	AMDSB_PMIO_INDEX		0xcd6
#define	AMDSB_PMIO_DATA			(PMIO_INDEX + 1)
#define	AMDSB_PMIO_WIDTH		2

/*
 * SB7x0 and compatible registers in the PMIO space.
 * See SB7xx RRG 2.3.3.2.
 */
#define	AMDSB_PM_RESET_STATUS0		0x44
#define	AMDSB_PM_RESET_STATUS1		0x45
#define		AMDSB_WD_RST_STS	0x02
#define	AMDSB_PM_WDT_CTRL		0x69
#define		AMDSB_WDT_DISABLE	0x01
#define		AMDSB_WDT_RES_MASK	(0x02 | 0x04)
#define		AMDSB_WDT_RES_32US	0x00
#define		AMDSB_WDT_RES_10MS	0x02
#define		AMDSB_WDT_RES_100MS	0x04
#define		AMDSB_WDT_RES_1S	0x06
#define	AMDSB_PM_WDT_BASE_LSB		0x6c
#define	AMDSB_PM_WDT_BASE_MSB		0x6f

/*
 * SB8x0 and compatible registers in the PMIO space.
 * See SB8xx RRG 2.3.3, for instance.
 */
#define	AMDSB8_PM_SMBUS_EN		0x2c
#define		AMDSB8_SMBUS_EN		0x01
#define		AMDSB8_SMBUS_ADDR_MASK	0xffe0u
#define	AMDSB8_PM_WDT_EN		0x48
#define		AMDSB8_WDT_DEC_EN	0x01
#define		AMDSB8_WDT_DISABLE	0x02
#define	AMDSB8_PM_WDT_CTRL		0x4c
#define		AMDSB8_WDT_32KHZ	0x00
#define		AMDSB8_WDT_1HZ		0x03
#define		AMDSB8_WDT_RES_MASK	0x03
#define	AMDSB8_PM_RESET_STATUS		0xc0	/* 32 bit wide */
#define		AMDSB8_WD_RST_STS	0x2000000
#define	AMDSB8_PM_RESET_CTRL		0xc4
#define		AMDSB8_RST_STS_DIS	0x04

/*
 * Newer FCH registers in the PMIO space.
 * See BKDG for Family 16h Models 30h-3Fh 3.26.13 PMx00 and PMx04.
 */
#define AMDFCH41_PM_DECODE_EN0		0x00
#define		AMDFCH41_SMBUS_EN	0x10
#define		AMDFCH41_WDT_EN		0x80
#define AMDFCH41_PM_DECODE_EN1		0x01
#define	AMDFCH41_PM_DECODE_EN3		0x03
#define		AMDFCH41_WDT_RES_MASK	0x03
#define		AMDFCH41_WDT_RES_32US	0x00
#define		AMDFCH41_WDT_RES_10MS	0x01
#define		AMDFCH41_WDT_RES_100MS	0x02
#define		AMDFCH41_WDT_RES_1S	0x03
#define		AMDFCH41_WDT_EN_MASK	0x0c
#define		AMDFCH41_WDT_ENABLE	0x00
#define	AMDFCH41_PM_ISA_CTRL		0x04
#define		AMDFCH41_MMIO_EN	0x02

/*
 * Fixed MMIO addresses for accessing Watchdog and SMBus registers.
 * See BKDG for Family 16h Models 30h-3Fh 3.26.13 PMx00 and PMx04.
 */
#define	AMDFCH41_WDT_FIXED_ADDR		0xfeb00000u
#define	AMDFCH41_MMIO_ADDR		0xfed80000u
#define AMDFCH41_MMIO_SMBUS_OFF		0x0a00
#define AMDFCH41_MMIO_WDT_OFF		0x0b00

/*
 * PCI Device IDs and revisions.
 * SB600 RRG 2.3.1.1,
 * SB7xx RRG 2.3.1.1,
 * SB8xx RRG 2.3.1,
 * BKDG for Family 15h Models 60h-6Fh 3.26.6.1,
 * BKDG for Family 15h Models 70h-7Fh 3.26.6.1,
 * BKDG for Family 16h Models 00h-0Fh 3.26.7.1,
 * BKDG for Family 16h Models 30h-3Fh 3.26.7.1.
 * Also, see i2c-piix4 aka piix4_smbus Linux driver.
 */
#define	AMDSB_SMBUS_DEVID		0x43851002
#define	AMDSB8_SMBUS_REVID		0x40
#define	AMDFCH_SMBUS_DEVID		0x780b1022
#define	AMDFCH41_SMBUS_REVID		0x41
#define	AMDCZ_SMBUS_DEVID		0x790b1022
#define	AMDCZ49_SMBUS_REVID		0x49

