/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_CORES_CHIPC_CHIPC_H_
#define _BHND_CORES_CHIPC_CHIPC_H_

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/nvram/bhnd_nvram.h>

#include "bhnd_chipc_if.h"

/**
 * Supported ChipCommon flash types.
 */
typedef enum {
	CHIPC_FLASH_NONE	= 0,	/**< No flash, or a type unrecognized
					     by the ChipCommon driver */
	CHIPC_PFLASH_CFI	= 1,	/**< CFI-compatible parallel flash */
	CHIPC_SFLASH_ST		= 2,	/**< ST serial flash */
	CHIPC_SFLASH_AT		= 3,	/**< Atmel serial flash */
	CHIPC_QSFLASH_ST	= 4,	/**< ST quad-SPI flash */ 
	CHIPC_QSFLASH_AT	= 5,	/**< Atmel quad-SPI flash */
	CHIPC_NFLASH		= 6,	/**< NAND flash */
	CHIPC_NFLASH_4706	= 7	/**< BCM4706 NAND flash */
} chipc_flash;

/**
 * ChipCommon capability flags;
 */
struct chipc_caps {
	uint8_t		num_uarts;	/**< Number of attached UARTS (1-3) */
	bool		mipseb;		/**< MIPS is big-endian */
	uint8_t		uart_clock;	/**< UART clock source (see CHIPC_CAP_UCLKSEL_*) */
	uint8_t		uart_gpio;	/**< UARTs own GPIO pins 12-15 */

	uint8_t		extbus_type;	/**< ExtBus type (CHIPC_CAP_EXTBUS_*) */

	chipc_flash 	flash_type;	/**< flash type */
	uint8_t		cfi_width;	/**< CFI bus width, 0 if unknown or CFI
					     not present */

	bhnd_nvram_src	nvram_src;	/**< identified NVRAM source */
	bus_size_t	sprom_offset;	/**< Offset to SPROM data within
					     SPROM/OTP, 0 if unknown or not
					     present */
	uint8_t		otp_size;	/**< OTP (row?) size, 0 if not present */

	uint8_t		pll_type;	/**< PLL type */
	bool		pwr_ctrl;	/**< Power/clock control available */
	bool		jtag_master;	/**< JTAG Master present */
	bool		boot_rom;	/**< Internal boot ROM is active */
	uint8_t		backplane_64;	/**< Backplane supports 64-bit addressing.
					     Note that this does not gaurantee
					     the CPU itself supports 64-bit
					     addressing. */
	bool		pmu;		/**< PMU is present. */
	bool		eci;		/**< ECI (enhanced coexistence inteface) is present. */
	bool		seci;		/**< SECI (serial ECI) is present */
	bool		sprom;		/**< SPROM is present */
	bool		gsio;		/**< GSIO (SPI/I2C) present */
	bool		aob;		/**< AOB (always on bus) present.
					     If set, PMU and GCI registers are
					     not accessible via ChipCommon,
					     and are instead accessible via
					     dedicated cores on the bhnd bus */
};

#endif /* _BHND_CORES_CHIPC_CHIPC_H_ */
