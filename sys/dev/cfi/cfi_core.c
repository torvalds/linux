/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007, Juniper Networks, Inc.
 * Copyright (c) 2012-2013, SRI International
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * (FA8750-10-C-0237) ("CTSRD"), as part of the DARPA CRASH research
 * programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cfi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kenv.h>
#include <sys/kernel.h>
#include <sys/malloc.h>   
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/cfi/cfi_reg.h>
#include <dev/cfi/cfi_var.h>

static void cfi_add_sysctls(struct cfi_softc *);

extern struct cdevsw cfi_cdevsw;

char cfi_driver_name[] = "cfi";
devclass_t cfi_devclass;
devclass_t cfi_diskclass;

uint32_t
cfi_read_raw(struct cfi_softc *sc, u_int ofs)
{
	uint32_t val;

	ofs &= ~(sc->sc_width - 1);
	switch (sc->sc_width) {
	case 1:
		val = bus_space_read_1(sc->sc_tag, sc->sc_handle, ofs);
		break;
	case 2:
		val = bus_space_read_2(sc->sc_tag, sc->sc_handle, ofs);
		break;
	case 4:
		val = bus_space_read_4(sc->sc_tag, sc->sc_handle, ofs);
		break;
	default:
		val = ~0;
		break;
	}
	return (val);
}

uint32_t
cfi_read(struct cfi_softc *sc, u_int ofs)
{
	uint32_t val;
	uint16_t sval;

	ofs &= ~(sc->sc_width - 1);
	switch (sc->sc_width) {
	case 1:
		val = bus_space_read_1(sc->sc_tag, sc->sc_handle, ofs);
		break;
	case 2:
		sval = bus_space_read_2(sc->sc_tag, sc->sc_handle, ofs);
#ifdef CFI_HARDWAREBYTESWAP
		val = sval;
#else
		val = le16toh(sval);
#endif
		break;
	case 4:
		val = bus_space_read_4(sc->sc_tag, sc->sc_handle, ofs);
#ifndef CFI_HARDWAREBYTESWAP
		val = le32toh(val);
#endif
		break;
	default:
		val = ~0;
		break;
	}
	return (val);
}

static void
cfi_write(struct cfi_softc *sc, u_int ofs, u_int val)
{

	ofs &= ~(sc->sc_width - 1);
	switch (sc->sc_width) {
	case 1:
		bus_space_write_1(sc->sc_tag, sc->sc_handle, ofs, val);
		break;
	case 2:
#ifdef CFI_HARDWAREBYTESWAP
		bus_space_write_2(sc->sc_tag, sc->sc_handle, ofs, val);
#else
		bus_space_write_2(sc->sc_tag, sc->sc_handle, ofs, htole16(val));

#endif
		break;
	case 4:
#ifdef CFI_HARDWAREBYTESWAP
		bus_space_write_4(sc->sc_tag, sc->sc_handle, ofs, val);
#else
		bus_space_write_4(sc->sc_tag, sc->sc_handle, ofs, htole32(val));
#endif
		break;
	}
}

/*
 * This is same workaound as NetBSD sys/dev/nor/cfi.c cfi_reset_default()
 */
static void
cfi_reset_default(struct cfi_softc *sc)
{

	cfi_write(sc, 0, CFI_BCS_READ_ARRAY2);
	cfi_write(sc, 0, CFI_BCS_READ_ARRAY);
}

uint8_t
cfi_read_qry(struct cfi_softc *sc, u_int ofs)
{
	uint8_t val;
 
	cfi_write(sc, CFI_QRY_CMD_ADDR * sc->sc_width, CFI_QRY_CMD_DATA); 
	val = cfi_read(sc, ofs * sc->sc_width);
	cfi_reset_default(sc);
	return (val);
} 

static void
cfi_amd_write(struct cfi_softc *sc, u_int ofs, u_int addr, u_int data)
{

	cfi_write(sc, ofs + AMD_ADDR_START, CFI_AMD_UNLOCK);
	cfi_write(sc, ofs + AMD_ADDR_ACK, CFI_AMD_UNLOCK_ACK);
	cfi_write(sc, ofs + addr, data);
}

static char *
cfi_fmtsize(uint32_t sz)
{
	static char buf[8];
	static const char *sfx[] = { "", "K", "M", "G" };
	int sfxidx;

	sfxidx = 0;
	while (sfxidx < 3 && sz > 1023) {
		sz /= 1024;
		sfxidx++;
	}

	sprintf(buf, "%u%sB", sz, sfx[sfxidx]);
	return (buf);
}

int
cfi_probe(device_t dev)
{
	char desc[80];
	struct cfi_softc *sc;
	char *vend_str;
	int error;
	uint16_t iface, vend;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->sc_rid = 0;
	sc->sc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rid,
	    RF_ACTIVE);
	if (sc->sc_res == NULL)
		return (ENXIO);

	sc->sc_tag = rman_get_bustag(sc->sc_res);
	sc->sc_handle = rman_get_bushandle(sc->sc_res);

	if (sc->sc_width == 0) {
		sc->sc_width = 1;
		while (sc->sc_width <= 4) {
			if (cfi_read_qry(sc, CFI_QRY_IDENT) == 'Q')
				break;
			sc->sc_width <<= 1;
		}
	} else if (cfi_read_qry(sc, CFI_QRY_IDENT) != 'Q') {
		error = ENXIO;
		goto out;
	}
	if (sc->sc_width > 4) {
		error = ENXIO;
		goto out;
	}

	/* We got a Q. Check if we also have the R and the Y. */
	if (cfi_read_qry(sc, CFI_QRY_IDENT + 1) != 'R' ||
	    cfi_read_qry(sc, CFI_QRY_IDENT + 2) != 'Y') {
		error = ENXIO;
		goto out;
	}

	/* Get the vendor and command set. */
	vend = cfi_read_qry(sc, CFI_QRY_VEND) |
	    (cfi_read_qry(sc, CFI_QRY_VEND + 1) << 8);

	sc->sc_cmdset = vend;

	switch (vend) {
	case CFI_VEND_AMD_ECS:
	case CFI_VEND_AMD_SCS:
		vend_str = "AMD/Fujitsu";
		break;
	case CFI_VEND_INTEL_ECS:
		vend_str = "Intel/Sharp";
		break;
	case CFI_VEND_INTEL_SCS:
		vend_str = "Intel";
		break;
	case CFI_VEND_MITSUBISHI_ECS:
	case CFI_VEND_MITSUBISHI_SCS:
		vend_str = "Mitsubishi";
		break;
	default:
		vend_str = "Unknown vendor";
		break;
	}

	/* Get the device size. */
	sc->sc_size = 1U << cfi_read_qry(sc, CFI_QRY_SIZE);

	/* Sanity-check the I/F */
	iface = cfi_read_qry(sc, CFI_QRY_IFACE) |
	    (cfi_read_qry(sc, CFI_QRY_IFACE + 1) << 8);

	/*
	 * Adding 1 to iface will give us a bit-wise "switch"
	 * that allows us to test for the interface width by
	 * testing a single bit.
	 */
	iface++;

	error = (iface & sc->sc_width) ? 0 : EINVAL;
	if (error)
		goto out;

	snprintf(desc, sizeof(desc), "%s - %s", vend_str,
	    cfi_fmtsize(sc->sc_size));
	device_set_desc_copy(dev, desc);

 out:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rid, sc->sc_res);
	return (error);
}

int
cfi_attach(device_t dev) 
{
	struct cfi_softc *sc;
	u_int blksz, blocks;
	u_int r, u;
	uint64_t mtoexp, ttoexp;
#ifdef CFI_SUPPORT_STRATAFLASH
	uint64_t ppr;
	char name[KENV_MNAMELEN], value[32];
#endif

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->sc_rid = 0;
	sc->sc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rid,
#ifndef ATSE_CFI_HACK
	    RF_ACTIVE);
#else
	    RF_ACTIVE | RF_SHAREABLE);
#endif
	if (sc->sc_res == NULL)
		return (ENXIO);

	sc->sc_tag = rman_get_bustag(sc->sc_res);
	sc->sc_handle = rman_get_bushandle(sc->sc_res);

	/* Get time-out values for erase, write, and buffer write. */
	ttoexp = cfi_read_qry(sc, CFI_QRY_TTO_ERASE);
	mtoexp = cfi_read_qry(sc, CFI_QRY_MTO_ERASE);
	if (ttoexp == 0) {
		device_printf(dev, "erase timeout == 0, using 2^16ms\n");
		ttoexp = 16;
	}
	if (ttoexp > 41) {
		device_printf(dev, "insane timeout: 2^%jdms\n", ttoexp);
		return (EINVAL);
	}
	if (mtoexp == 0) {
		device_printf(dev, "max erase timeout == 0, using 2^%jdms\n",
		    ttoexp + 4);
		mtoexp = 4;
	}
	if (ttoexp + mtoexp > 41) {
		device_printf(dev, "insane max erase timeout: 2^%jd\n",
		    ttoexp + mtoexp);
		return (EINVAL);
	}
	sc->sc_typical_timeouts[CFI_TIMEOUT_ERASE] = SBT_1MS * (1ULL << ttoexp);
	sc->sc_max_timeouts[CFI_TIMEOUT_ERASE] =
	    sc->sc_typical_timeouts[CFI_TIMEOUT_ERASE] * (1ULL << mtoexp);

	ttoexp = cfi_read_qry(sc, CFI_QRY_TTO_WRITE);
	mtoexp = cfi_read_qry(sc, CFI_QRY_MTO_WRITE);
	if (ttoexp == 0) {
		device_printf(dev, "write timeout == 0, using 2^18ns\n");
		ttoexp = 18;
	}
	if (ttoexp > 51) {
		device_printf(dev, "insane write timeout: 2^%jdus\n", ttoexp);
		return (EINVAL);
	}
	if (mtoexp == 0) {
		device_printf(dev, "max write timeout == 0, using 2^%jdms\n",
		    ttoexp + 4);
		mtoexp = 4;
	}
	if (ttoexp + mtoexp > 51) {
		device_printf(dev, "insane max write timeout: 2^%jdus\n",
		    ttoexp + mtoexp);
		return (EINVAL);
	}
	sc->sc_typical_timeouts[CFI_TIMEOUT_WRITE] = SBT_1US * (1ULL << ttoexp);
	sc->sc_max_timeouts[CFI_TIMEOUT_WRITE] =
	    sc->sc_typical_timeouts[CFI_TIMEOUT_WRITE] * (1ULL << mtoexp);

	ttoexp = cfi_read_qry(sc, CFI_QRY_TTO_BUFWRITE);
	mtoexp = cfi_read_qry(sc, CFI_QRY_MTO_BUFWRITE);
	/* Don't check for 0, it means not-supported. */
	if (ttoexp > 51) {
		device_printf(dev, "insane write timeout: 2^%jdus\n", ttoexp);
		return (EINVAL);
	}
	if (ttoexp + mtoexp > 51) {
		device_printf(dev, "insane max write timeout: 2^%jdus\n",
		    ttoexp + mtoexp);
		return (EINVAL);
	}
	sc->sc_typical_timeouts[CFI_TIMEOUT_BUFWRITE] =
	    SBT_1US * (1ULL << cfi_read_qry(sc, CFI_QRY_TTO_BUFWRITE));
	sc->sc_max_timeouts[CFI_TIMEOUT_BUFWRITE] =
	    sc->sc_typical_timeouts[CFI_TIMEOUT_BUFWRITE] *
	    (1ULL << cfi_read_qry(sc, CFI_QRY_MTO_BUFWRITE));

	/* Get the maximum size of a multibyte program */
	if (sc->sc_typical_timeouts[CFI_TIMEOUT_BUFWRITE] != 0)
		sc->sc_maxbuf = 1 << (cfi_read_qry(sc, CFI_QRY_MAXBUF) |
		    cfi_read_qry(sc, CFI_QRY_MAXBUF) << 8);
	else
		sc->sc_maxbuf = 0;

	/* Get erase regions. */
	sc->sc_regions = cfi_read_qry(sc, CFI_QRY_NREGIONS);
	sc->sc_region = malloc(sc->sc_regions * sizeof(struct cfi_region),
	    M_TEMP, M_WAITOK | M_ZERO);
	for (r = 0; r < sc->sc_regions; r++) {
		blocks = cfi_read_qry(sc, CFI_QRY_REGION(r)) |
		    (cfi_read_qry(sc, CFI_QRY_REGION(r) + 1) << 8);
		sc->sc_region[r].r_blocks = blocks + 1;

		blksz = cfi_read_qry(sc, CFI_QRY_REGION(r) + 2) |
		    (cfi_read_qry(sc, CFI_QRY_REGION(r) + 3) << 8);
		sc->sc_region[r].r_blksz = (blksz == 0) ? 128 :
		    blksz * 256;
	}

	/* Reset the device to a default state. */
	cfi_write(sc, 0, CFI_BCS_CLEAR_STATUS);

	if (bootverbose) {
		device_printf(dev, "[");
		for (r = 0; r < sc->sc_regions; r++) {
			printf("%ux%s%s", sc->sc_region[r].r_blocks,
			    cfi_fmtsize(sc->sc_region[r].r_blksz),
			    (r == sc->sc_regions - 1) ? "]\n" : ",");
		}
	}

	if (sc->sc_cmdset == CFI_VEND_AMD_ECS  ||
	    sc->sc_cmdset == CFI_VEND_AMD_SCS) {
		cfi_amd_write(sc, 0, AMD_ADDR_START, CFI_AMD_AUTO_SELECT);
		sc->sc_manid = cfi_read(sc, 0);
		sc->sc_devid = cfi_read(sc, 2);
		device_printf(dev, "Manufacturer ID:%x Device ID:%x\n",
		    sc->sc_manid, sc->sc_devid);
		cfi_write(sc, 0, CFI_BCS_READ_ARRAY2);
	}

	u = device_get_unit(dev);
	sc->sc_nod = make_dev(&cfi_cdevsw, u, UID_ROOT, GID_WHEEL, 0600,
	    "%s%u", cfi_driver_name, u);
	sc->sc_nod->si_drv1 = sc;

	cfi_add_sysctls(sc);

#ifdef CFI_SUPPORT_STRATAFLASH
	/*
	 * Store the Intel factory PPR in the environment.  In some
	 * cases it is the most unique ID on a board.
	 */
	if (cfi_intel_get_factory_pr(sc, &ppr) == 0) {
		if (snprintf(name, sizeof(name), "%s.factory_ppr",
		    device_get_nameunit(dev)) < (sizeof(name) - 1) &&
		    snprintf(value, sizeof(value), "0x%016jx", ppr) <
		    (sizeof(value) - 1))
			(void) kern_setenv(name, value);
	}
#endif

	device_add_child(dev, "cfid", -1);
	bus_generic_attach(dev);

	return (0);
}

static void
cfi_add_sysctls(struct cfi_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;

	ctx = device_get_sysctl_ctx(sc->sc_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev));

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO,
	    "typical_erase_timout_count",
	    CTLFLAG_RD, &sc->sc_tto_counts[CFI_TIMEOUT_ERASE],
	    0, "Number of times the typical erase timeout was exceeded");
	SYSCTL_ADD_UINT(ctx, children, OID_AUTO,
	    "max_erase_timout_count",
	    CTLFLAG_RD, &sc->sc_mto_counts[CFI_TIMEOUT_ERASE], 0,
	    "Number of times the maximum erase timeout was exceeded");
	SYSCTL_ADD_UINT(ctx, children, OID_AUTO,
	    "typical_write_timout_count",
	    CTLFLAG_RD, &sc->sc_tto_counts[CFI_TIMEOUT_WRITE], 0,
	    "Number of times the typical write timeout was exceeded");
	SYSCTL_ADD_UINT(ctx, children, OID_AUTO,
	    "max_write_timout_count",
	    CTLFLAG_RD, &sc->sc_mto_counts[CFI_TIMEOUT_WRITE], 0,
	    "Number of times the maximum write timeout was exceeded");
	if (sc->sc_maxbuf > 0) {
		SYSCTL_ADD_UINT(ctx, children, OID_AUTO,
		    "typical_bufwrite_timout_count",
		    CTLFLAG_RD, &sc->sc_tto_counts[CFI_TIMEOUT_BUFWRITE], 0,
		    "Number of times the typical buffered write timeout was "
		    "exceeded");
		SYSCTL_ADD_UINT(ctx, children, OID_AUTO,
		    "max_bufwrite_timout_count",
		    CTLFLAG_RD, &sc->sc_mto_counts[CFI_TIMEOUT_BUFWRITE], 0,
		    "Number of times the maximum buffered write timeout was "
		    "exceeded");
	}
}

int
cfi_detach(device_t dev)
{
	struct cfi_softc *sc;

	sc = device_get_softc(dev);

	destroy_dev(sc->sc_nod);
	free(sc->sc_region, M_TEMP);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rid, sc->sc_res);
	return (0);
}

static bool
cfi_check_erase(struct cfi_softc *sc, u_int ofs, u_int sz)
{
	bool result;
	int i;
	uint32_t val;

	result = FALSE;
	for (i = 0; i < sz; i += sc->sc_width) {
		val = cfi_read(sc, ofs + i);
		switch (sc->sc_width) {
		case 1:
			if (val != 0xff)
				goto out;
			continue;
		case 2:
			if (val != 0xffff)
				goto out;
			continue;
		case 4:
			if (val != 0xffffffff)
				goto out;
			continue;
		}
	}
	result = TRUE;

out:
	return (result);
}

static int
cfi_wait_ready(struct cfi_softc *sc, u_int ofs, sbintime_t start,
    enum cfi_wait_cmd cmd)
{
	int done, error, tto_exceeded;
	uint32_t st0 = 0, st = 0;
	sbintime_t now;

	done = 0;
	error = 0;
	tto_exceeded = 0;
	while (!done && !error) {
		/*
		 * Save time before we start so we always do one check
		 * after the timeout has expired.
		 */
		now = sbinuptime();

		switch (sc->sc_cmdset) {
		case CFI_VEND_INTEL_ECS:
		case CFI_VEND_INTEL_SCS:
			st = cfi_read(sc, ofs);
			done = (st & CFI_INTEL_STATUS_WSMS);
			if (done) {
				/* NB: bit 0 is reserved */
				st &= ~(CFI_INTEL_XSTATUS_RSVD |
					CFI_INTEL_STATUS_WSMS |
					CFI_INTEL_STATUS_RSVD);
				if (st & CFI_INTEL_STATUS_DPS)
					error = EPERM;
				else if (st & CFI_INTEL_STATUS_PSLBS)
					error = EIO;
				else if (st & CFI_INTEL_STATUS_ECLBS)
					error = ENXIO;
				else if (st)
					error = EACCES;
			}
			break;
		case CFI_VEND_AMD_SCS:
		case CFI_VEND_AMD_ECS:
			st0 = cfi_read(sc, ofs);
			st = cfi_read(sc, ofs);
			done = ((st & 0x40) == (st0 & 0x40)) ? 1 : 0;
			break;
		}

		if (tto_exceeded ||
		    now > start + sc->sc_typical_timeouts[cmd]) {
			if (!tto_exceeded) {
				tto_exceeded = 1;
				sc->sc_tto_counts[cmd]++;
#ifdef CFI_DEBUG_TIMEOUT
				device_printf(sc->sc_dev,
				    "typical timeout exceeded (cmd %d)", cmd);
#endif
			}
			if (now > start + sc->sc_max_timeouts[cmd]) {
				sc->sc_mto_counts[cmd]++;
#ifdef CFI_DEBUG_TIMEOUT
				device_printf(sc->sc_dev,
				    "max timeout exceeded (cmd %d)", cmd);
#endif
			}
		}
	}
	if (!done && !error)
		error = ETIMEDOUT;
	if (error)
		printf("\nerror=%d (st 0x%x st0 0x%x)\n", error, st, st0);
	return (error);
}

int
cfi_write_block(struct cfi_softc *sc)
{
	union {
		uint8_t		*x8;
		uint16_t	*x16;
		uint32_t	*x32;
	} ptr, cpyprt;
	register_t intr;
	int error, i, j, neederase = 0;
	uint32_t st;
	u_int wlen;
	sbintime_t start;
	u_int minsz;
	uint32_t val;

	/* Intel flash must be unlocked before modification */
	switch (sc->sc_cmdset) {
	case CFI_VEND_INTEL_ECS:
	case CFI_VEND_INTEL_SCS:
		cfi_write(sc, sc->sc_wrofs, CFI_INTEL_LBS);
		cfi_write(sc, sc->sc_wrofs, CFI_INTEL_UB);
		cfi_write(sc, sc->sc_wrofs, CFI_BCS_READ_ARRAY);
		break;
	}

	/* Check if an erase is required. */
	for (i = 0; i < sc->sc_wrbufsz; i++)
		if ((sc->sc_wrbuf[i] & sc->sc_wrbufcpy[i]) != sc->sc_wrbuf[i]) {
			neederase = 1;
			break;
		}

	if (neederase) {
		intr = intr_disable();
		start = sbinuptime();
		/* Erase the block. */
		switch (sc->sc_cmdset) {
		case CFI_VEND_INTEL_ECS:
		case CFI_VEND_INTEL_SCS:
			cfi_write(sc, sc->sc_wrofs, CFI_BCS_BLOCK_ERASE);
			cfi_write(sc, sc->sc_wrofs, CFI_BCS_CONFIRM);
			break;
		case CFI_VEND_AMD_SCS:
		case CFI_VEND_AMD_ECS:
			/* find minimum sector size */
			minsz = sc->sc_region[0].r_blksz;
			for (i = 1; i < sc->sc_regions; i++) {
				if (sc->sc_region[i].r_blksz < minsz)
					minsz = sc->sc_region[i].r_blksz;
			}
			cfi_amd_write(sc, sc->sc_wrofs, AMD_ADDR_START,
			    CFI_AMD_ERASE_SECTOR);
			cfi_amd_write(sc, sc->sc_wrofs, 
			    sc->sc_wrofs >> (ffs(minsz) - 1),
			    CFI_AMD_BLOCK_ERASE);
			for (i = 0; i < CFI_AMD_MAXCHK; ++i) {
				if (cfi_check_erase(sc, sc->sc_wrofs,
				    sc->sc_wrbufsz))
					break;
				DELAY(10);
			}
			if (i == CFI_AMD_MAXCHK) {
				printf("\nCFI Sector Erase time out error\n");
				return (ENODEV);
			}
			break;
		default:
			/* Better safe than sorry... */
			intr_restore(intr);
			return (ENODEV);
		}
		intr_restore(intr);
		error = cfi_wait_ready(sc, sc->sc_wrofs, start, 
		    CFI_TIMEOUT_ERASE);
		if (error)
			goto out;
	} else
		error = 0;

	/* Write the block using a multibyte write if supported. */
	ptr.x8 = sc->sc_wrbuf;
	cpyprt.x8 = sc->sc_wrbufcpy;
	if (sc->sc_maxbuf > sc->sc_width) {
		switch (sc->sc_cmdset) {
		case CFI_VEND_INTEL_ECS:
		case CFI_VEND_INTEL_SCS:
			for (i = 0; i < sc->sc_wrbufsz; i += wlen) {
				wlen = MIN(sc->sc_maxbuf, sc->sc_wrbufsz - i);

				intr = intr_disable();

				start = sbinuptime();
				do {
					cfi_write(sc, sc->sc_wrofs + i,
					    CFI_BCS_BUF_PROG_SETUP);
					if (sbinuptime() > start + sc->sc_max_timeouts[CFI_TIMEOUT_BUFWRITE]) {
						error = ETIMEDOUT;
						goto out;
					}
					st = cfi_read(sc, sc->sc_wrofs + i);
				} while (! (st & CFI_INTEL_STATUS_WSMS));

				cfi_write(sc, sc->sc_wrofs + i,
				    (wlen / sc->sc_width) - 1);
				switch (sc->sc_width) {
				case 1:
					bus_space_write_region_1(sc->sc_tag,
					    sc->sc_handle, sc->sc_wrofs + i,
					    ptr.x8 + i, wlen);
					break;
				case 2:
					bus_space_write_region_2(sc->sc_tag,
					    sc->sc_handle, sc->sc_wrofs + i,
					    ptr.x16 + i / 2, wlen / 2);
					break;
				case 4:
					bus_space_write_region_4(sc->sc_tag,
					    sc->sc_handle, sc->sc_wrofs + i,
					    ptr.x32 + i / 4, wlen / 4);
					break;
				}

				cfi_write(sc, sc->sc_wrofs + i,
				    CFI_BCS_CONFIRM);

				intr_restore(intr);

				error = cfi_wait_ready(sc, sc->sc_wrofs + i,
				    start, CFI_TIMEOUT_BUFWRITE);
				if (error != 0)
					goto out;
			}
			goto out;
		default:
			/* Fall through to single word case */
			break;
		}

	}

	/* Write the block one byte/word at a time. */
	for (i = 0; i < sc->sc_wrbufsz; i += sc->sc_width) {

		/* Avoid writing unless we are actually changing bits */
		if (!neederase) {
			switch (sc->sc_width) {
			case 1:
				if(*(ptr.x8 + i) == *(cpyprt.x8 + i))
					continue;
				break;
			case 2:
				if(*(ptr.x16 + i / 2) == *(cpyprt.x16 + i / 2))
					continue;
				break;
			case 4:
				if(*(ptr.x32 + i / 4) == *(cpyprt.x32 + i / 4))
					continue;
				break;
			}
		}

		/*
		 * Make sure the command to start a write and the
		 * actual write happens back-to-back without any
		 * excessive delays.
		 */
		intr = intr_disable();

		start = sbinuptime();
		switch (sc->sc_cmdset) {
		case CFI_VEND_INTEL_ECS:
		case CFI_VEND_INTEL_SCS:
			cfi_write(sc, sc->sc_wrofs + i, CFI_BCS_PROGRAM);
			break;
		case CFI_VEND_AMD_SCS:
		case CFI_VEND_AMD_ECS:
			cfi_amd_write(sc, 0, AMD_ADDR_START, CFI_AMD_PROGRAM);
			break;
		}
		switch (sc->sc_width) {
		case 1:
			bus_space_write_1(sc->sc_tag, sc->sc_handle,
			    sc->sc_wrofs + i, *(ptr.x8 + i));
			break;
		case 2:
			bus_space_write_2(sc->sc_tag, sc->sc_handle,
			    sc->sc_wrofs + i, *(ptr.x16 + i / 2));
			break;
		case 4:
			bus_space_write_4(sc->sc_tag, sc->sc_handle,
			    sc->sc_wrofs + i, *(ptr.x32 + i / 4));
			break;
		}
		
		intr_restore(intr);

		if (sc->sc_cmdset == CFI_VEND_AMD_ECS  ||
		    sc->sc_cmdset == CFI_VEND_AMD_SCS) {
			for (j = 0; j < CFI_AMD_MAXCHK; ++j) {
				switch (sc->sc_width) {
				case 1:
					val = *(ptr.x8 + i);
					break;
				case 2:
					val = *(ptr.x16 + i / 2);
					break;
				case 4:
					val = *(ptr.x32 + i / 4);
					break;
				}

				if (cfi_read(sc, sc->sc_wrofs + i) == val)
					break;
					
				DELAY(10);
			}
			if (j == CFI_AMD_MAXCHK) {
				printf("\nCFI Program Verify time out error\n");
				error = ENXIO;
				goto out;
			}
		} else {
			error = cfi_wait_ready(sc, sc->sc_wrofs, start,
			   CFI_TIMEOUT_WRITE);
			if (error)
				goto out;
		}
	}

	/* error is 0. */

 out:
	cfi_reset_default(sc);

	/* Relock Intel flash */
	switch (sc->sc_cmdset) {
	case CFI_VEND_INTEL_ECS:
	case CFI_VEND_INTEL_SCS:
		cfi_write(sc, sc->sc_wrofs, CFI_INTEL_LBS);
		cfi_write(sc, sc->sc_wrofs, CFI_INTEL_LB);
		cfi_write(sc, sc->sc_wrofs, CFI_BCS_READ_ARRAY);
		break;
	}
	return (error);
}

#ifdef CFI_SUPPORT_STRATAFLASH
/*
 * Intel StrataFlash Protection Register Support.
 *
 * The memory includes a 128-bit Protection Register that can be
 * used for security.  There are two 64-bit segments; one is programmed
 * at the factory with a unique 64-bit number which is immutable.
 * The other segment is left blank for User (OEM) programming.
 * The User/OEM segment is One Time Programmable (OTP).  It can also
 * be locked to prevent any further writes by setting bit 0 of the
 * Protection Lock Register (PLR).  The PLR can written only once.
 */

static uint16_t
cfi_get16(struct cfi_softc *sc, int off)
{
	uint16_t v = bus_space_read_2(sc->sc_tag, sc->sc_handle, off<<1);
	return v;
}

#ifdef CFI_ARMEDANDDANGEROUS
static void
cfi_put16(struct cfi_softc *sc, int off, uint16_t v)
{
	bus_space_write_2(sc->sc_tag, sc->sc_handle, off<<1, v);
}
#endif

/*
 * Read the factory-defined 64-bit segment of the PR.
 */
int 
cfi_intel_get_factory_pr(struct cfi_softc *sc, uint64_t *id)
{
	if (sc->sc_cmdset != CFI_VEND_INTEL_ECS)
		return EOPNOTSUPP;
	KASSERT(sc->sc_width == 2, ("sc_width %d", sc->sc_width));

	cfi_write(sc, 0, CFI_INTEL_READ_ID);
	*id = ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(0)))<<48 |
	      ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(1)))<<32 |
	      ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(2)))<<16 |
	      ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(3)));
	cfi_write(sc, 0, CFI_BCS_READ_ARRAY);
	return 0;
}

/*
 * Read the User/OEM 64-bit segment of the PR.
 */
int 
cfi_intel_get_oem_pr(struct cfi_softc *sc, uint64_t *id)
{
	if (sc->sc_cmdset != CFI_VEND_INTEL_ECS)
		return EOPNOTSUPP;
	KASSERT(sc->sc_width == 2, ("sc_width %d", sc->sc_width));

	cfi_write(sc, 0, CFI_INTEL_READ_ID);
	*id = ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(4)))<<48 |
	      ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(5)))<<32 |
	      ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(6)))<<16 |
	      ((uint64_t)cfi_get16(sc, CFI_INTEL_PR(7)));
	cfi_write(sc, 0, CFI_BCS_READ_ARRAY);
	return 0;
}

/*
 * Write the User/OEM 64-bit segment of the PR.
 * XXX should allow writing individual words/bytes
 */
int
cfi_intel_set_oem_pr(struct cfi_softc *sc, uint64_t id)
{
#ifdef CFI_ARMEDANDDANGEROUS
	register_t intr;
	int i, error;
	sbintime_t start;
#endif

	if (sc->sc_cmdset != CFI_VEND_INTEL_ECS)
		return EOPNOTSUPP;
	KASSERT(sc->sc_width == 2, ("sc_width %d", sc->sc_width));

#ifdef CFI_ARMEDANDDANGEROUS
	for (i = 7; i >= 4; i--, id >>= 16) {
		intr = intr_disable();
		start = sbinuptime();
		cfi_write(sc, 0, CFI_INTEL_PP_SETUP);
		cfi_put16(sc, CFI_INTEL_PR(i), id&0xffff);
		intr_restore(intr);
		error = cfi_wait_ready(sc, CFI_BCS_READ_STATUS, start,
		    CFI_TIMEOUT_WRITE);
		if (error)
			break;
	}
	cfi_write(sc, 0, CFI_BCS_READ_ARRAY);
	return error;
#else
	device_printf(sc->sc_dev, "%s: OEM PR not set, "
	    "CFI_ARMEDANDDANGEROUS not configured\n", __func__);
	return ENXIO;
#endif
}

/*
 * Read the contents of the Protection Lock Register.
 */
int 
cfi_intel_get_plr(struct cfi_softc *sc, uint32_t *plr)
{
	if (sc->sc_cmdset != CFI_VEND_INTEL_ECS)
		return EOPNOTSUPP;
	KASSERT(sc->sc_width == 2, ("sc_width %d", sc->sc_width));

	cfi_write(sc, 0, CFI_INTEL_READ_ID);
	*plr = cfi_get16(sc, CFI_INTEL_PLR);
	cfi_write(sc, 0, CFI_BCS_READ_ARRAY);
	return 0;
}

/*
 * Write the Protection Lock Register to lock down the
 * user-settable segment of the Protection Register.
 * NOTE: this operation is not reversible.
 */
int 
cfi_intel_set_plr(struct cfi_softc *sc)
{
#ifdef CFI_ARMEDANDDANGEROUS
	register_t intr;
	int error;
	sbintime_t start;
#endif
	if (sc->sc_cmdset != CFI_VEND_INTEL_ECS)
		return EOPNOTSUPP;
	KASSERT(sc->sc_width == 2, ("sc_width %d", sc->sc_width));

#ifdef CFI_ARMEDANDDANGEROUS
	/* worthy of console msg */
	device_printf(sc->sc_dev, "set PLR\n");
	intr = intr_disable();
	binuptime(&start);
	cfi_write(sc, 0, CFI_INTEL_PP_SETUP);
	cfi_put16(sc, CFI_INTEL_PLR, 0xFFFD);
	intr_restore(intr);
	error = cfi_wait_ready(sc, CFI_BCS_READ_STATUS, start,
	    CFI_TIMEOUT_WRITE);
	cfi_write(sc, 0, CFI_BCS_READ_ARRAY);
	return error;
#else
	device_printf(sc->sc_dev, "%s: PLR not set, "
	    "CFI_ARMEDANDDANGEROUS not configured\n", __func__);
	return ENXIO;
#endif
}
#endif /* CFI_SUPPORT_STRATAFLASH */
