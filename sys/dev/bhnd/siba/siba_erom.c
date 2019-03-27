/*-
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/bhnd/bhnd_eromvar.h>

#include <dev/bhnd/cores/chipc/chipcreg.h>

#include "sibareg.h"
#include "sibavar.h"

#include "siba_eromvar.h"

struct siba_erom;
struct siba_erom_io;


static int			siba_eio_init(struct siba_erom_io *io,
				    struct bhnd_erom_io *eio, u_int ncores);

static uint32_t			siba_eio_read_4(struct siba_erom_io *io,
				    u_int core_idx, bus_size_t offset);

static int			siba_eio_read_core_id(struct siba_erom_io *io,
				    u_int core_idx, int unit,
				    struct siba_core_id *sid);

static int			siba_eio_read_chipid(struct siba_erom_io *io,
				    bus_addr_t enum_addr,
				    struct bhnd_chipid *cid);

/**
 * SIBA EROM generic I/O context
 */
struct siba_erom_io {
	struct bhnd_erom_io	*eio;		/**< erom I/O callbacks */
	bhnd_addr_t		 base_addr;	/**< address of first core */
	u_int			 ncores;	/**< core count */
};

/**
 * SIBA EROM per-instance state.
 */
struct siba_erom {
	struct bhnd_erom	obj;
	struct siba_erom_io	io;	/**< i/o context */
};

#define	EROM_LOG(io, fmt, ...)	do {				\
	printf("%s: " fmt, __FUNCTION__, ##__VA_ARGS__);	\
} while(0)

/* SIBA implementation of BHND_EROM_PROBE() */
static int
siba_erom_probe(bhnd_erom_class_t *cls, struct bhnd_erom_io *eio,
    const struct bhnd_chipid *hint, struct bhnd_chipid *cid)
{
	struct siba_erom_io	io;
	uint32_t		idreg;
	int			error;

	/* Initialize I/O context, assuming at least the first core is mapped */
	if ((error = siba_eio_init(&io, eio, 1)))
		return (error);

	/* Try using the provided hint. */
	if (hint != NULL) {
		struct siba_core_id sid;

		/* Validate bus type */
		if (hint->chip_type != BHND_CHIPTYPE_SIBA)
			return (ENXIO);

		/*
		 * Verify the first core's IDHIGH/IDLOW identification.
		 * 
		 * The core must be a Broadcom core, but must *not* be
		 * a chipcommon core; those shouldn't be hinted.
		 *
		 * The first core on EXTIF-equipped devices varies, but on the
		 * BCM4710, it's a SDRAM core (0x803).
		 */

		if ((error = siba_eio_read_core_id(&io, 0, 0, &sid)))
			return (error);

		if (sid.core_info.vendor != BHND_MFGID_BCM)
			return (ENXIO);

		if (sid.core_info.device == BHND_COREID_CC)
			return (EINVAL);

		*cid = *hint;
	} else {
		/* Validate bus type */
		idreg = siba_eio_read_4(&io, 0, CHIPC_ID);
		if (CHIPC_GET_BITS(idreg, CHIPC_ID_BUS) != BHND_CHIPTYPE_SIBA)
			return (ENXIO);	

		/* Identify the chipset */
		if ((error = siba_eio_read_chipid(&io, SIBA_ENUM_ADDR, cid)))
			return (error);

		/* Verify the chip type */
		if (cid->chip_type != BHND_CHIPTYPE_SIBA)
			return (ENXIO);
	}

	/*
	 * gcc hack: ensure bhnd_chipid.ncores cannot exceed SIBA_MAX_CORES
	 * without triggering build failure due to -Wtype-limits
	 *
	 * if (cid.ncores > SIBA_MAX_CORES)
	 *      return (EINVAL)
	 */
	_Static_assert((2^sizeof(cid->ncores)) <= SIBA_MAX_CORES,
	    "ncores could result in over-read of backing resource");

	return (0);
}

/* SIBA implementation of BHND_EROM_INIT() */
static int
siba_erom_init(bhnd_erom_t *erom, const struct bhnd_chipid *cid,
    struct bhnd_erom_io *eio)
{
	struct siba_erom	*sc;
	int			 error;

	sc = (struct siba_erom *)erom;

	/* Attempt to map the full core enumeration space */
	error = bhnd_erom_io_map(eio, cid->enum_addr,
	    cid->ncores * SIBA_CORE_SIZE);
	if (error) {
		printf("%s: failed to map %u cores: %d\n", __FUNCTION__,
		    cid->ncores, error);
		return (error);
	}

	/* Initialize I/O context */
	return (siba_eio_init(&sc->io, eio, cid->ncores));
}

/* SIBA implementation of BHND_EROM_FINI() */
static void
siba_erom_fini(bhnd_erom_t *erom)
{
	struct siba_erom *sc = (struct siba_erom *)erom;

	bhnd_erom_io_fini(sc->io.eio);
}

/* Initialize siba_erom resource I/O context */
static int
siba_eio_init(struct siba_erom_io *io, struct bhnd_erom_io *eio, u_int ncores)
{
	io->eio = eio;
	io->ncores = ncores;
	return (0);
}

/**
 * Read a 32-bit value from @p offset relative to the base address of
 * the given @p core_idx.
 * 
 * @param io EROM I/O context.
 * @param core_idx Core index.
 * @param offset Core register offset.
 */
static uint32_t
siba_eio_read_4(struct siba_erom_io *io, u_int core_idx, bus_size_t offset)
{
	/* Sanity check core index and offset */
	if (core_idx >= io->ncores)
		panic("core index %u out of range (ncores=%u)", core_idx,
		    io->ncores);

	if (offset > SIBA_CORE_SIZE - sizeof(uint32_t))
		panic("invalid core offset %#jx", (uintmax_t)offset);

	/* Perform read */
	return (bhnd_erom_io_read(io->eio, SIBA_CORE_OFFSET(core_idx) + offset,
	    4));
}

/**
 * Read and parse identification registers for the given @p core_index.
 * 
 * @param io EROM I/O context.
 * @param core_idx The core index.
 * @param unit The caller-specified unit number to be included in the return
 * value.
 * @param[out] sid On success, the parsed siba core id.
 * 
 * @retval 0		success
 * @retval non-zero     if reading or parsing the identification registers
 *			otherwise fails, a regular unix error code will be
 *			returned.
 */
static int
siba_eio_read_core_id(struct siba_erom_io *io, u_int core_idx, int unit,
    struct siba_core_id *sid)
{
	struct siba_admatch	admatch[SIBA_MAX_ADDRSPACE];
	uint32_t		idhigh, idlow;
	uint32_t		tpsflag;
	uint16_t		ocp_vendor;
	uint8_t			sonics_rev;
	uint8_t			num_admatch;
	uint8_t			num_admatch_en;
	uint8_t			num_cfg;
	bool			intr_en;
	u_int			intr_flag;
	int			error;

	idhigh = siba_eio_read_4(io, core_idx, SB0_REG_ABS(SIBA_CFG0_IDHIGH));
	idlow = siba_eio_read_4(io, core_idx, SB0_REG_ABS(SIBA_CFG0_IDLOW));
	tpsflag = siba_eio_read_4(io, core_idx, SB0_REG_ABS(SIBA_CFG0_TPSFLAG));

	ocp_vendor = SIBA_REG_GET(idhigh, IDH_VENDOR);
	sonics_rev = SIBA_REG_GET(idlow, IDL_SBREV);
	num_admatch = SIBA_REG_GET(idlow, IDL_NRADDR) + 1 /* + enum block */;
	if (num_admatch > nitems(admatch)) {
		printf("core%u: invalid admatch count %hhu\n", core_idx,
		    num_admatch);
		return (EINVAL);
	}

	/* Determine backplane interrupt distribution configuration */
	intr_en = ((tpsflag & SIBA_TPS_F0EN0) != 0);
	intr_flag = SIBA_REG_GET(tpsflag, TPS_NUM0);

	/* Determine the number of sonics config register blocks */
	num_cfg = SIBA_CFG_NUM_2_2;
	if (sonics_rev >= SIBA_IDL_SBREV_2_3)
		num_cfg = SIBA_CFG_NUM_2_3;

	/* Parse all admatch descriptors */
	num_admatch_en = 0;
	for (uint8_t i = 0; i < num_admatch; i++) {
		uint32_t	am_value;
		u_int		am_offset;

		KASSERT(i < nitems(admatch), ("invalid admatch index"));

		/* Determine the register offset */
		am_offset = siba_admatch_offset(i);
		if (am_offset == 0) {
			printf("core%u: addrspace %hhu is unsupported",
			    core_idx, i);
			return (ENODEV);
		}

		/* Read and parse the address match register */
		am_value = siba_eio_read_4(io, core_idx, am_offset);
		error = siba_parse_admatch(am_value, &admatch[num_admatch_en]);
		if (error) {
			printf("core%u: failed to decode admatch[%hhu] "
			    "register value 0x%x\n", core_idx, i, am_value);
			return (error);
		}

		/* Skip disabled entries */
		if (!admatch[num_admatch_en].am_enabled)
			continue;

		/* Reject unsupported negative matches. These are not used on
		 * any known devices */
		if (admatch[num_admatch_en].am_negative) {
			printf("core%u: unsupported negative admatch[%hhu] "
			    "value 0x%x\n", core_idx, i, am_value);
			return (ENXIO);
		}

		num_admatch_en++;
	}

	/* Populate the result */
	*sid = (struct siba_core_id) {
		.core_info	= {
			.vendor	= siba_get_bhnd_mfgid(ocp_vendor),
			.device	= SIBA_REG_GET(idhigh, IDH_DEVICE),
			.hwrev	= SIBA_IDH_CORE_REV(idhigh),
			.core_idx = core_idx,
			.unit	= unit
		},
		.sonics_vendor	= ocp_vendor,
		.sonics_rev	= sonics_rev,
		.intr_en	= intr_en,
		.intr_flag	= intr_flag,
		.num_admatch	= num_admatch_en,
		.num_cfg_blocks	= num_cfg
	};
	memcpy(sid->admatch, admatch, num_admatch_en * sizeof(admatch[0]));

	return (0);
}

/**
 * Read and parse the SSB identification registers for the given @p core_index,
 * returning the siba(4) core identification in @p sid.
 * 
 * @param sc A siba EROM instance.
 * @param core_idx The index of the core to be identified.
 * @param[out] result On success, the parsed siba core id.
 * 
 * @retval 0		success
 * @retval non-zero     if reading or parsing the identification registers
 *			otherwise fails, a regular unix error code will be
 *			returned.
 */
int
siba_erom_get_core_id(struct siba_erom *sc, u_int core_idx,
    struct siba_core_id *result)
{
	struct siba_core_id	sid;
	int			error;

	/* Fetch the core info, assuming a unit number of 0 */
	if ((error = siba_eio_read_core_id(&sc->io, core_idx, 0, &sid)))
		return (error);

	/* Scan preceding cores to determine the real unit number. */
	for (u_int i = 0; i < core_idx; i++) {
		struct siba_core_id prev;

		if ((error = siba_eio_read_core_id(&sc->io, i, 0, &prev)))
			return (error);

		/* Bump the unit number? */
		if (sid.core_info.vendor == prev.core_info.vendor &&
		    sid.core_info.device == prev.core_info.device)
			sid.core_info.unit++;
	}

	*result = sid;
	return (0);
}

/**
 * Read and parse the chip identification register from the ChipCommon core.
 * 
 * @param io EROM I/O context.
 * @param enum_addr The physical address mapped by @p io.
 * @param cid On success, the parsed chip identifier.
 */
static int
siba_eio_read_chipid(struct siba_erom_io *io, bus_addr_t enum_addr,
    struct bhnd_chipid *cid)
{
	struct siba_core_id	ccid;
	int			error;

	/* Identify the chipcommon core */
	if ((error = siba_eio_read_core_id(io, 0, 0, &ccid)))
		return (error);

	if (ccid.core_info.vendor != BHND_MFGID_BCM ||
	    ccid.core_info.device != BHND_COREID_CC)
	{
		if (bootverbose) {
			EROM_LOG(io, "first core not chipcommon "
			    "(vendor=%#hx, core=%#hx)\n", ccid.core_info.vendor,
			    ccid.core_info.device);
		}
		return (ENXIO);
	}

	/* Identify the chipset */
	if ((error = bhnd_erom_read_chipid(io->eio, cid)))
		return (error);

	/* Do we need to fix up the core count? */
	if (CHIPC_NCORES_MIN_HWREV(ccid.core_info.hwrev))
		return (0);

	switch (cid->chip_id) {
	case BHND_CHIPID_BCM4306:
		cid->ncores = 6;
		break;
	case BHND_CHIPID_BCM4704:
		cid->ncores = 9;
		break;
	case BHND_CHIPID_BCM5365:
		/*
		* BCM5365 does support ID_NUMCORE in at least
		* some of its revisions, but for unknown
		* reasons, Broadcom's drivers always exclude
		* the ChipCommon revision (0x5) used by BCM5365
		* from the set of revisions supporting
		* ID_NUMCORE, and instead supply a fixed value.
		* 
		* Presumably, at least some of these devices
		* shipped with a broken ID_NUMCORE value.
		*/
		cid->ncores = 7;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static int
siba_erom_lookup_core(bhnd_erom_t *erom, const struct bhnd_core_match *desc,
    struct bhnd_core_info *core)
{
	struct siba_erom	*sc;
	struct bhnd_core_match	 imatch;
	int			 error;

	sc = (struct siba_erom *)erom;

	/* We can't determine a core's unit number during the initial scan. */
	imatch = *desc;
	imatch.m.match.core_unit = 0;

	/* Locate the first matching core */
	for (u_int i = 0; i < sc->io.ncores; i++) {
		struct siba_core_id	sid;
		struct bhnd_core_info	ci;

		/* Read the core info */
		if ((error = siba_eio_read_core_id(&sc->io, i, 0, &sid)))
			return (error);

		ci = sid.core_info;

		/* Check for initial match */
		if (!bhnd_core_matches(&ci, &imatch))
			continue;

		/* Re-scan preceding cores to determine the unit number. */
		for (u_int j = 0; j < i; j++) {
			error = siba_eio_read_core_id(&sc->io, j, 0, &sid);
			if (error)
				return (error);

			/* Bump the unit number? */
			if (sid.core_info.vendor == ci.vendor &&
			    sid.core_info.device == ci.device)
				ci.unit++;
		}

		/* Check for full match against now-valid unit number */
		if (!bhnd_core_matches(&ci, desc))
			continue;

		/* Matching core found */
		*core = ci;
		return (0);
	}

	/* Not found */
	return (ENOENT);
}

static int
siba_erom_lookup_core_addr(bhnd_erom_t *erom, const struct bhnd_core_match *desc,
    bhnd_port_type type, u_int port, u_int region, struct bhnd_core_info *info,
    bhnd_addr_t *addr, bhnd_size_t *size)
{
	struct siba_erom	*sc;
	struct bhnd_core_info	 core;
	struct siba_core_id	 sid;
	struct siba_admatch	 admatch;
	uint32_t		 am;
	u_int			 am_offset;
	u_int			 addrspace, cfg;
	
	int			 error;

	sc = (struct siba_erom *)erom;

	/* Locate the requested core */
	if ((error = siba_erom_lookup_core(erom, desc, &core)))
		return (error);

	/* Fetch full siba core ident */
	error = siba_eio_read_core_id(&sc->io, core.core_idx, core.unit, &sid);
	if (error)
		return (error);

	/* Is port valid? */
	if (!siba_is_port_valid(&sid, type, port))
		return (ENOENT);

	/* Is region valid? */
	if (region >= siba_port_region_count(&sid, type, port))
		return (ENOENT);

	/* Is this a siba configuration region? If so, this is mapped to an
	 * offset within the device0.0 port */
	error = siba_cfg_index(&sid, type, port, region, &cfg);
	if (!error) {
		bhnd_addr_t	region_addr;
		bhnd_addr_t	region_size;
		bhnd_size_t	cfg_offset, cfg_size;

		cfg_offset = SIBA_CFG_OFFSET(cfg);
		cfg_size = SIBA_CFG_SIZE;

		/* Fetch the device0.0 addr/size */
		error = siba_erom_lookup_core_addr(erom, desc, BHND_PORT_DEVICE,
		    0, 0, NULL, &region_addr, &region_size);
		if (error)
			return (error);

		/* Verify that our offset fits within the region */
		if (region_size < cfg_size) {
			printf("%s%u.%u offset %ju exceeds %s0.0 size %ju\n",
			    bhnd_port_type_name(type), port, region, cfg_offset,
			    bhnd_port_type_name(BHND_PORT_DEVICE), region_size);

			return (ENXIO);
		}

		if (BHND_ADDR_MAX - region_addr < cfg_offset) {
			printf("%s%u.%u offset %ju would overflow %s0.0 addr "
			    "%ju\n", bhnd_port_type_name(type), port, region,
			    cfg_offset, bhnd_port_type_name(BHND_PORT_DEVICE),
			    region_addr);

			return (ENXIO);
		}

		if (info != NULL)
			*info = core;

		*addr = region_addr + cfg_offset;
		*size = cfg_size;
		return (0);
	}

	/* 
	 * Otherwise, must be a device port.
	 * 
	 * Map the bhnd device port to a siba addrspace index. Unlike siba(4)
	 * bus drivers, we do not exclude the siba(4) configuration blocks from
	 * the first device port.
	 */
	error = siba_addrspace_index(&sid, type, port, region, &addrspace);
	if (error)
		return (error);

	/* Determine the register offset */
	am_offset = siba_admatch_offset(addrspace);
	if (am_offset == 0) {
		printf("addrspace %u is unsupported", addrspace);
		return (ENODEV);
	}

	/* Read and parse the address match register */
	am = siba_eio_read_4(&sc->io, core.core_idx, am_offset);

	if ((error = siba_parse_admatch(am, &admatch))) {
		printf("failed to decode address match register value 0x%x\n",
		    am);
		return (error);
	}

	if (info != NULL)
		*info = core;

	*addr = admatch.am_base;
	*size = admatch.am_size;

	return (0);
}

/* BHND_EROM_GET_CORE_TABLE() */
static int
siba_erom_get_core_table(bhnd_erom_t *erom, struct bhnd_core_info **cores,
    u_int *num_cores)
{
	struct siba_erom	*sc;
	struct bhnd_core_info	*out;
	int			 error;

	sc = (struct siba_erom *)erom;

	/* Allocate our core array */
	out = mallocarray(sc->io.ncores, sizeof(*out), M_BHND, M_NOWAIT);
	if (out == NULL)
		return (ENOMEM);

	*cores = out;
	*num_cores = sc->io.ncores;

	/* Enumerate all cores. */
	for (u_int i = 0; i < sc->io.ncores; i++) {
		struct siba_core_id sid;

		/* Read the core info */
		if ((error = siba_eio_read_core_id(&sc->io, i, 0, &sid)))
			return (error);

		out[i] = sid.core_info;

		/* Determine unit number */
		for (u_int j = 0; j < i; j++) {
			if (out[j].vendor == out[i].vendor &&
			    out[j].device == out[i].device)
				out[i].unit++;
		}
	}

	return (0);
}

/* BHND_EROM_FREE_CORE_TABLE() */
static void
siba_erom_free_core_table(bhnd_erom_t *erom, struct bhnd_core_info *cores)
{
	free(cores, M_BHND);
}

/* BHND_EROM_DUMP() */
static int
siba_erom_dump(bhnd_erom_t *erom)
{
	struct siba_erom	*sc;
	int			 error;

	sc = (struct siba_erom *)erom;

	/* Enumerate all cores. */
	for (u_int i = 0; i < sc->io.ncores; i++) {
		uint32_t idhigh, idlow;
		uint32_t nraddr;

		idhigh = siba_eio_read_4(&sc->io, i,
		    SB0_REG_ABS(SIBA_CFG0_IDHIGH));
		idlow = siba_eio_read_4(&sc->io, i,
		    SB0_REG_ABS(SIBA_CFG0_IDLOW));

		printf("siba core %u:\n", i);
		printf("\tvendor:\t0x%04x\n", SIBA_REG_GET(idhigh, IDH_VENDOR));
		printf("\tdevice:\t0x%04x\n", SIBA_REG_GET(idhigh, IDH_DEVICE));
		printf("\trev:\t0x%04x\n", SIBA_IDH_CORE_REV(idhigh));
		printf("\tsbrev:\t0x%02x\n", SIBA_REG_GET(idlow, IDL_SBREV));

		/* Enumerate the address match registers */
		nraddr = SIBA_REG_GET(idlow, IDL_NRADDR);
		printf("\tnraddr\t0x%04x\n", nraddr);

		for (size_t addrspace = 0; addrspace < nraddr; addrspace++) {
			struct siba_admatch	admatch;
			uint32_t		am;
			u_int			am_offset;

			/* Determine the register offset */
			am_offset = siba_admatch_offset(addrspace);
			if (am_offset == 0) {
				printf("addrspace %zu unsupported",
				    addrspace);
				break;
			}
			
			/* Read and parse the address match register */
			am = siba_eio_read_4(&sc->io, i, am_offset);
			if ((error = siba_parse_admatch(am, &admatch))) {
				printf("failed to decode address match "
				    "register value 0x%x\n", am);
				continue;
			}

			printf("\taddrspace %zu\n", addrspace);
			printf("\t\taddr: 0x%08x\n", admatch.am_base);
			printf("\t\tsize: 0x%08x\n", admatch.am_size);
		}
	}

	return (0);
}

static kobj_method_t siba_erom_methods[] = {
	KOBJMETHOD(bhnd_erom_probe,		siba_erom_probe),
	KOBJMETHOD(bhnd_erom_init,		siba_erom_init),
	KOBJMETHOD(bhnd_erom_fini,		siba_erom_fini),
	KOBJMETHOD(bhnd_erom_get_core_table,	siba_erom_get_core_table),
	KOBJMETHOD(bhnd_erom_free_core_table,	siba_erom_free_core_table),
	KOBJMETHOD(bhnd_erom_lookup_core,	siba_erom_lookup_core),
	KOBJMETHOD(bhnd_erom_lookup_core_addr,	siba_erom_lookup_core_addr),
	KOBJMETHOD(bhnd_erom_dump,		siba_erom_dump),

	KOBJMETHOD_END
};

BHND_EROM_DEFINE_CLASS(siba_erom, siba_erom_parser, siba_erom_methods, sizeof(struct siba_erom));
