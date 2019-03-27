/*-
 * Copyright (c) 2010-2016 Solarflare Communications, Inc.
 * All rights reserved.
 *
 * This software was developed in part by OKTET Labs Ltd. under contract for
 * Solarflare Communications, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include <sys/types.h>
#include <sys/malloc.h>

#include "common/efx.h"
#include "sfxge.h"

/* These data make no real sense, they are here just to make sfupdate happy.
 * Any code that would rely on it is broken.
 */
static const uint8_t fake_dynamic_cfg_nvram[] = {
	0x7a, 0xda, 0x10, 0xef, 0x0c, 0x00, 0x00, 0x00,
	0x00, 0x05, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x10,
	0x08, 0x00, 0x00, 0x00, 0x90, 0x04, 0x00, 0x52,
	0x56, 0x01, 0xc3, 0x78, 0x01, 0x00, 0x03, 0x10,
	0x08, 0x00, 0x00, 0x00, 0x90, 0x04, 0x00, 0x52,
	0x56, 0x01, 0xc3, 0x78, 0x57, 0x1a, 0x10, 0xef,
	0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x02, 0x0b, 0x64, 0x7d, 0xee, 0xee, 0xee, 0xee
};

static int
sfxge_nvram_rw(struct sfxge_softc *sc, sfxge_ioc_t *ip, efx_nvram_type_t type,
	       boolean_t write)
{
	efx_nic_t *enp = sc->enp;
	size_t total_size = ip->u.nvram.size;
	size_t chunk_size;
	off_t off;
	int rc = 0;
	uint8_t *buf;

	if (type == EFX_NVRAM_DYNAMIC_CFG && sc->family == EFX_FAMILY_SIENA) {
		if (write)
			return (0);
		rc = copyout(fake_dynamic_cfg_nvram, ip->u.nvram.data,
			     MIN(total_size, sizeof(fake_dynamic_cfg_nvram)));
		return (rc);
	}

	if ((rc = efx_nvram_rw_start(enp, type, &chunk_size)) != 0)
		goto fail1;

	buf = malloc(chunk_size, M_TEMP, M_WAITOK);

	off = 0;
	while (total_size) {
		size_t len = MIN(chunk_size, total_size);

		if (write) {
			rc = copyin(ip->u.nvram.data + off, buf, len);
			if (rc != 0)
				goto fail3;
			rc = efx_nvram_write_chunk(enp, type,
						   ip->u.nvram.offset + off, buf, len);
			if (rc != 0)
				goto fail3;
		} else {
			rc = efx_nvram_read_chunk(enp, type,
						  ip->u.nvram.offset + off, buf, len);
			if (rc != 0)
				goto fail3;
			rc = copyout(buf, ip->u.nvram.data + off, len);
			if (rc != 0)
				goto fail3;
		}

		total_size -= len;
		off += len;
	}

fail3:
	free(buf, M_TEMP);
	efx_nvram_rw_finish(enp, type, NULL);
fail1:
	return (rc);
}


static int
sfxge_nvram_erase(struct sfxge_softc *sc, efx_nvram_type_t type)
{
	efx_nic_t *enp = sc->enp;
	size_t chunk_size;
	int rc = 0;

	if (type == EFX_NVRAM_DYNAMIC_CFG && sc->family == EFX_FAMILY_SIENA)
		return (0);

	if ((rc = efx_nvram_rw_start(enp, type, &chunk_size)) != 0)
		return (rc);

	rc = efx_nvram_erase(enp, type);

	efx_nvram_rw_finish(enp, type, NULL);
	return (rc);
}

int
sfxge_nvram_ioctl(struct sfxge_softc *sc, sfxge_ioc_t *ip)
{
	static const efx_nvram_type_t nvram_types[] = {
		[SFXGE_NVRAM_TYPE_BOOTROM]  = EFX_NVRAM_BOOTROM,
		[SFXGE_NVRAM_TYPE_BOOTROM_CFG]  = EFX_NVRAM_BOOTROM_CFG,
		[SFXGE_NVRAM_TYPE_MC]  = EFX_NVRAM_MC_FIRMWARE,
		[SFXGE_NVRAM_TYPE_MC_GOLDEN]  = EFX_NVRAM_MC_GOLDEN,
		[SFXGE_NVRAM_TYPE_PHY]  = EFX_NVRAM_PHY,
		[SFXGE_NVRAM_TYPE_NULL_PHY]  = EFX_NVRAM_NULLPHY,
		[SFXGE_NVRAM_TYPE_FPGA]  = EFX_NVRAM_FPGA,
		[SFXGE_NVRAM_TYPE_FCFW]  = EFX_NVRAM_FCFW,
		[SFXGE_NVRAM_TYPE_CPLD]  = EFX_NVRAM_CPLD,
		[SFXGE_NVRAM_TYPE_FPGA_BACKUP]  = EFX_NVRAM_FPGA_BACKUP,
		[SFXGE_NVRAM_TYPE_DYNAMIC_CFG]  = EFX_NVRAM_DYNAMIC_CFG,
	};

	efx_nic_t *enp = sc->enp;
	efx_nvram_type_t type;
	int rc = 0;

	if (ip->u.nvram.type > SFXGE_NVRAM_TYPE_DYNAMIC_CFG)
		return (EINVAL);
	type = nvram_types[ip->u.nvram.type];
	if (type == EFX_NVRAM_MC_GOLDEN &&
	    (ip->u.nvram.op == SFXGE_NVRAM_OP_WRITE ||
	     ip->u.nvram.op == SFXGE_NVRAM_OP_ERASE ||
	     ip->u.nvram.op == SFXGE_NVRAM_OP_SET_VER))
		return (EOPNOTSUPP);

	switch (ip->u.nvram.op) {
	case SFXGE_NVRAM_OP_SIZE:
	{
		size_t size;

		if (type == EFX_NVRAM_DYNAMIC_CFG && sc->family == EFX_FAMILY_SIENA) {
			ip->u.nvram.size = sizeof(fake_dynamic_cfg_nvram);
		} else {
			if ((rc = efx_nvram_size(enp, type, &size)) != 0)
				return (rc);
			ip->u.nvram.size = size;
		}
		break;
	}
	case SFXGE_NVRAM_OP_READ:
		rc = sfxge_nvram_rw(sc, ip, type, B_FALSE);
		break;
	case SFXGE_NVRAM_OP_WRITE:
		rc = sfxge_nvram_rw(sc, ip, type, B_TRUE);
		break;
	case SFXGE_NVRAM_OP_ERASE:
		rc = sfxge_nvram_erase(sc, type);
		break;
	case SFXGE_NVRAM_OP_GET_VER:
		rc = efx_nvram_get_version(enp, type, &ip->u.nvram.subtype,
					   &ip->u.nvram.version[0]);
		break;
	case SFXGE_NVRAM_OP_SET_VER:
		rc = efx_nvram_set_version(enp, type, &ip->u.nvram.version[0]);
		break;
	default:
		rc = EOPNOTSUPP;
		break;
	}

	return (rc);
}
