/*-
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
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

#ifndef _BHND_NVRAM_BHND_NVRAM_BCMREG_H_
#define _BHND_NVRAM_BHND_NVRAM_BCMREG_H_

#define BCM_NVRAM_GET_BITS(_value, _field)			\
	((_value & _field ## _MASK) >> _field ## _SHIFT)

#define BCM_NVRAM_SET_BITS(_value, _field, _bits)		\
	((_value & ~(_field ## _MASK)) |			\
	    (((_bits) << _field ## _SHIFT) & _field ## _MASK))

/* BCM NVRAM header fields */
#define	BCM_NVRAM_MAGIC				0x48534C46	/* 'FLSH' */
#define	BCM_NVRAM_VERSION			1

#define	BCM_NVRAM_CRC_SKIP			9		/* skip magic, size, and crc8 */

#define	BCM_NVRAM_CFG0_CRC_MASK			0x000000FF
#define	BCM_NVRAM_CFG0_CRC_SHIFT		0
#define	BCM_NVRAM_CFG0_VER_MASK			0x0000FF00
#define	BCM_NVRAM_CFG0_VER_SHIFT		8
#define	BCM_NVRAM_CFG0_VER_DEFAULT		1		/* default version */

#define	BCM_NVRAM_CFG0_SDRAM_INIT_FIELD		cfg0
#define	BCM_NVRAM_CFG0_SDRAM_INIT_MASK		0xFFFF0000
#define	BCM_NVRAM_CFG0_SDRAM_INIT_SHIFT		16
#define	BCM_NVRAM_CFG0_SDRAM_INIT_VAR		"sdram_init"
#define	BCM_NVRAM_CFG0_SDRAM_INIT_FMT		"0x%04x"

#define	BCM_NVRAM_CFG1_SDRAM_CFG_FIELD		cfg1
#define	BCM_NVRAM_CFG1_SDRAM_CFG_MASK		0x0000FFFF
#define	BCM_NVRAM_CFG1_SDRAM_CFG_SHIFT		0
#define	BCM_NVRAM_CFG1_SDRAM_CFG_VAR		"sdram_config"
#define	BCM_NVRAM_CFG1_SDRAM_CFG_FMT		"0x%04x"

#define	BCM_NVRAM_CFG1_SDRAM_REFRESH_FIELD	cfg1
#define	BCM_NVRAM_CFG1_SDRAM_REFRESH_MASK	0xFFFF0000
#define	BCM_NVRAM_CFG1_SDRAM_REFRESH_SHIFT	16
#define	BCM_NVRAM_CFG1_SDRAM_REFRESH_VAR	"sdram_refresh"
#define	BCM_NVRAM_CFG1_SDRAM_REFRESH_FMT	"0x%04x"

#define	BCM_NVRAM_SDRAM_NCDL_FIELD		sdram_ncdl
#define	BCM_NVRAM_SDRAM_NCDL_MASK		UINT32_MAX
#define	BCM_NVRAM_SDRAM_NCDL_SHIFT		0
#define	BCM_NVRAM_SDRAM_NCDL_VAR		"sdram_ncdl"
#define	BCM_NVRAM_SDRAM_NCDL_FMT		"0x%08x"

#endif /* _BHND_NVRAM_BHND_NVRAM_BCMREG_H_ */
