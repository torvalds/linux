/*-
 * Copyright (c) 2014-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_SFXGE_IOC_H
#define	_SYS_SFXGE_IOC_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

/* More codes may be added if necessary */
enum sfxge_ioc_codes {
	SFXGE_MCDI_IOC,
	SFXGE_NVRAM_IOC,
	SFXGE_VPD_IOC
};

enum sfxge_nvram_ops {
	SFXGE_NVRAM_OP_SIZE,
	SFXGE_NVRAM_OP_READ,
	SFXGE_NVRAM_OP_WRITE,
	SFXGE_NVRAM_OP_ERASE,
	SFXGE_NVRAM_OP_GET_VER,
	SFXGE_NVRAM_OP_SET_VER
};

enum sfxge_nvram_types {
	SFXGE_NVRAM_TYPE_BOOTROM,
	SFXGE_NVRAM_TYPE_BOOTROM_CFG,
	SFXGE_NVRAM_TYPE_MC,
	SFXGE_NVRAM_TYPE_MC_GOLDEN,
	SFXGE_NVRAM_TYPE_PHY,
	SFXGE_NVRAM_TYPE_NULL_PHY,
	SFXGE_NVRAM_TYPE_FPGA,
	SFXGE_NVRAM_TYPE_FCFW,
	SFXGE_NVRAM_TYPE_CPLD,
	SFXGE_NVRAM_TYPE_FPGA_BACKUP,
	SFXGE_NVRAM_TYPE_DYNAMIC_CFG
};

enum sfxge_vpd_ops {
	SFXGE_VPD_OP_GET_KEYWORD,
	SFXGE_VPD_OP_SET_KEYWORD
};

#define	SFXGE_MCDI_MAX_PAYLOAD 0x400
#define	SFXGE_VPD_MAX_PAYLOAD 0x100

typedef struct sfxge_ioc_s {
	uint32_t	op;
	union {
		struct {
			caddr_t		payload;
			uint32_t	cmd;
			size_t		len; /* In and out */
			uint32_t	rc;
		} mcdi;
		struct {
			uint32_t	op;
			uint32_t	type;
			uint32_t	offset;
			uint32_t	size;
			uint32_t	subtype;
			uint16_t	version[4];		/* get/set_ver */
			caddr_t		data;
		} nvram;
		struct {
			uint8_t		op;
			uint8_t		tag;
			uint16_t	keyword;
			uint16_t		len; /* In or out */
			caddr_t		payload;
		} vpd;
	} u;
} __packed sfxge_ioc_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SFXGE_IOC_H */
