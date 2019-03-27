/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2017 Landon Fuller <landonf@landonf.org>
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
#include <sys/limits.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd_eromvar.h>

#include "bcma_eromreg.h"
#include "bcma_eromvar.h"

/*
 * BCMA Enumeration ROM (EROM) Table
 * 
 * Provides auto-discovery of BCMA cores on Broadcom's HND SoC.
 * 
 * The EROM core address can be found at BCMA_CC_EROM_ADDR within the
 * ChipCommon registers. The table itself is comprised of 32-bit
 * type-tagged entries, organized into an array of variable-length
 * core descriptor records.
 * 
 * The final core descriptor is followed by a 32-bit BCMA_EROM_TABLE_EOF (0xF)
 * marker.
 */

static const char	*bcma_erom_entry_type_name (uint8_t entry);

static int		 bcma_erom_read32(struct bcma_erom *erom,
			     uint32_t *entry);
static int		 bcma_erom_skip32(struct bcma_erom *erom);

static int		 bcma_erom_skip_core(struct bcma_erom *erom);
static int		 bcma_erom_skip_mport(struct bcma_erom *erom);
static int		 bcma_erom_skip_sport_region(struct bcma_erom *erom);

static int		 bcma_erom_seek_next(struct bcma_erom *erom,
			     uint8_t etype);
static int		 bcma_erom_region_to_port_type(struct bcma_erom *erom,
			     uint8_t region_type, bhnd_port_type *port_type);


static int		 bcma_erom_peek32(struct bcma_erom *erom,
			     uint32_t *entry);

static bus_size_t	 bcma_erom_tell(struct bcma_erom *erom);
static void		 bcma_erom_seek(struct bcma_erom *erom,
			     bus_size_t offset);
static void		 bcma_erom_reset(struct bcma_erom *erom);

static int		 bcma_erom_seek_matching_core(struct bcma_erom *sc,
			     const struct bhnd_core_match *desc,
			     struct bhnd_core_info *core);

static int		 bcma_erom_parse_core(struct bcma_erom *erom,
			     struct bcma_erom_core *core);

static int		 bcma_erom_parse_mport(struct bcma_erom *erom,
			     struct bcma_erom_mport *mport);

static int		 bcma_erom_parse_sport_region(struct bcma_erom *erom,
			     struct bcma_erom_sport_region *region);

static void		 bcma_erom_to_core_info(const struct bcma_erom_core *core,
			     u_int core_idx, int core_unit,
			     struct bhnd_core_info *info);

/**
 * BCMA EROM per-instance state.
 */
struct bcma_erom {
	struct bhnd_erom	 obj;
	device_t	 	 dev;		/**< parent device, or NULL if none. */
	struct bhnd_erom_io	*eio;		/**< bus I/O callbacks */
	bhnd_size_t	 	 offset;	/**< current read offset */
};

#define	EROM_LOG(erom, fmt, ...)	do {			\
	printf("%s erom[0x%llx]: " fmt, __FUNCTION__,		\
	    (unsigned long long)(erom->offset), ##__VA_ARGS__);	\
} while(0)

/** Return the type name for an EROM entry */
static const char *
bcma_erom_entry_type_name (uint8_t entry)
{
	switch (BCMA_EROM_GET_ATTR(entry, ENTRY_TYPE)) {
	case BCMA_EROM_ENTRY_TYPE_CORE:
		return "core";
	case BCMA_EROM_ENTRY_TYPE_MPORT:
		return "mport";
	case BCMA_EROM_ENTRY_TYPE_REGION:
		return "region";
	default:
		return "unknown";
	}
}

/* BCMA implementation of BHND_EROM_INIT() */
static int
bcma_erom_init(bhnd_erom_t *erom, const struct bhnd_chipid *cid,
    struct bhnd_erom_io *eio)
{
	struct bcma_erom	*sc;
	bhnd_addr_t		 table_addr;
	int			 error;

	sc = (struct bcma_erom *)erom;
	sc->eio = eio;
	sc->offset = 0;

	/* Determine erom table address */
	if (BHND_ADDR_MAX - BCMA_EROM_TABLE_START < cid->enum_addr)
		return (ENXIO); /* would overflow */

	table_addr = cid->enum_addr + BCMA_EROM_TABLE_START;

	/* Try to map the erom table */
	error = bhnd_erom_io_map(sc->eio, table_addr, BCMA_EROM_TABLE_SIZE);
	if (error)
		return (error);

	return (0);
}

/* BCMA implementation of BHND_EROM_PROBE() */
static int
bcma_erom_probe(bhnd_erom_class_t *cls, struct bhnd_erom_io *eio,
    const struct bhnd_chipid *hint, struct bhnd_chipid *cid)
{
	int error;

	/* Hints aren't supported; all BCMA devices have a ChipCommon
	 * core */
	if (hint != NULL)
		return (EINVAL);

	/* Read and parse chip identification */
	if ((error = bhnd_erom_read_chipid(eio, cid)))
		return (error);

	/* Verify chip type */
	switch (cid->chip_type) {
		case BHND_CHIPTYPE_BCMA:
			return (BUS_PROBE_DEFAULT);

		case BHND_CHIPTYPE_BCMA_ALT:
		case BHND_CHIPTYPE_UBUS:
			return (BUS_PROBE_GENERIC);

		default:
			return (ENXIO);
	}	
}

static void
bcma_erom_fini(bhnd_erom_t *erom)
{
	struct bcma_erom *sc = (struct bcma_erom *)erom;

	bhnd_erom_io_fini(sc->eio);
}

static int
bcma_erom_lookup_core(bhnd_erom_t *erom, const struct bhnd_core_match *desc,
    struct bhnd_core_info *core)
{
	struct bcma_erom *sc = (struct bcma_erom *)erom;

	/* Search for the first matching core */
	return (bcma_erom_seek_matching_core(sc, desc, core));
}

static int
bcma_erom_lookup_core_addr(bhnd_erom_t *erom, const struct bhnd_core_match *desc,
    bhnd_port_type port_type, u_int port_num, u_int region_num,
    struct bhnd_core_info *core, bhnd_addr_t *addr, bhnd_size_t *size)
{
	struct bcma_erom	*sc;
	struct bcma_erom_core	 ec;
	uint32_t		 entry;
	uint8_t			 region_port, region_type;
	bool			 found;
	int			 error;

	sc = (struct bcma_erom *)erom;

	/* Seek to the first matching core and provide the core info
	 * to the caller */
	if ((error = bcma_erom_seek_matching_core(sc, desc, core)))
		return (error);

	if ((error = bcma_erom_parse_core(sc, &ec)))
		return (error);

	/* Skip master ports */
	for (u_long i = 0; i < ec.num_mport; i++) {
		if ((error = bcma_erom_skip_mport(sc)))
			return (error);
	}

	/* Seek to the region block for the given port type */
	found = false;
	while (1) {
		bhnd_port_type	p_type;
		uint8_t		r_type;

		if ((error = bcma_erom_peek32(sc, &entry)))
			return (error);

		if (!BCMA_EROM_ENTRY_IS(entry, REGION))
			return (ENOENT);

		/* Expected region type? */
		r_type = BCMA_EROM_GET_ATTR(entry, REGION_TYPE);
		error = bcma_erom_region_to_port_type(sc, r_type, &p_type);
		if (error)
			return (error);

		if (p_type == port_type) {
			found = true;
			break;
		}

		/* Skip to next entry */
		if ((error = bcma_erom_skip_sport_region(sc)))
			return (error);
	}

	if (!found)
		return (ENOENT);

	/* Found the appropriate port type block; now find the region records
	 * for the given port number */
	found = false;
	for (u_int i = 0; i <= port_num; i++) {
		bhnd_port_type	p_type;

		if ((error = bcma_erom_peek32(sc, &entry)))
			return (error);
		
		if (!BCMA_EROM_ENTRY_IS(entry, REGION))
			return (ENOENT);

		/* Fetch the type/port of the first region entry */
		region_type = BCMA_EROM_GET_ATTR(entry, REGION_TYPE);
		region_port = BCMA_EROM_GET_ATTR(entry, REGION_PORT);

		/* Have we found the region entries for the desired port? */
		if (i == port_num) {
			error = bcma_erom_region_to_port_type(sc, region_type,
			    &p_type);
			if (error)
				return (error);

			if (p_type == port_type)
				found = true;

			break;
		}

		/* Otherwise, seek to next block of region records */
		while (1) {
			uint8_t	next_type, next_port;
	
			if ((error = bcma_erom_skip_sport_region(sc)))
				return (error);

			if ((error = bcma_erom_peek32(sc, &entry)))
				return (error);

			if (!BCMA_EROM_ENTRY_IS(entry, REGION))
				return (ENOENT);

			next_type = BCMA_EROM_GET_ATTR(entry, REGION_TYPE);
			next_port = BCMA_EROM_GET_ATTR(entry, REGION_PORT);

			if (next_type != region_type ||
			    next_port != region_port)
				break;
		}
	}

	if (!found)
		return (ENOENT);

	/* Finally, search for the requested region number */
	for (u_int i = 0; i <= region_num; i++) {
		struct bcma_erom_sport_region	region;
		uint8_t				next_port, next_type;

		if ((error = bcma_erom_peek32(sc, &entry)))
			return (error);
		
		if (!BCMA_EROM_ENTRY_IS(entry, REGION))
			return (ENOENT);

		/* Check for the end of the region block */
		next_type = BCMA_EROM_GET_ATTR(entry, REGION_TYPE);
		next_port = BCMA_EROM_GET_ATTR(entry, REGION_PORT);

		if (next_type != region_type ||
		    next_port != region_port)
			break;

		/* Parse the region */
		if ((error = bcma_erom_parse_sport_region(sc, &region)))
			return (error);

		/* Is this our target region_num? */
		if (i == region_num) {
			/* Found */
			*addr = region.base_addr;
			*size = region.size;
			return (0);
		}
	}

	/* Not found */
	return (ENOENT);
};

static int
bcma_erom_get_core_table(bhnd_erom_t *erom, struct bhnd_core_info **cores,
    u_int *num_cores)
{
	struct bcma_erom	*sc;
	struct bhnd_core_info	*buffer;
	bus_size_t		 initial_offset;
	u_int			 count;
	int			 error;

	sc = (struct bcma_erom *)erom;

	buffer = NULL;
	initial_offset = bcma_erom_tell(sc);

	/* Determine the core count */
	bcma_erom_reset(sc);
	for (count = 0, error = 0; !error; count++) {
		struct bcma_erom_core core;

		/* Seek to the first readable core entry */
		error = bcma_erom_seek_next(sc, BCMA_EROM_ENTRY_TYPE_CORE);
		if (error == ENOENT)
			break;
		else if (error)
			goto cleanup;
		
		/* Read past the core descriptor */
		if ((error = bcma_erom_parse_core(sc, &core)))
			goto cleanup;
	}

	/* Allocate our output buffer */
	buffer = mallocarray(count, sizeof(struct bhnd_core_info), M_BHND,
	    M_NOWAIT);
	if (buffer == NULL) {
		error = ENOMEM;
		goto cleanup;
	}

	/* Parse all core descriptors */
	bcma_erom_reset(sc);
	for (u_int i = 0; i < count; i++) {
		struct bcma_erom_core	core;
		int			unit;

		/* Parse the core */
		error = bcma_erom_seek_next(sc, BCMA_EROM_ENTRY_TYPE_CORE);
		if (error)
			goto cleanup;

		error = bcma_erom_parse_core(sc, &core);
		if (error)
			goto cleanup;

		/* Determine the unit number */
		unit = 0;
		for (u_int j = 0; j < i; j++) {
			if (buffer[i].vendor == buffer[j].vendor &&
			    buffer[i].device == buffer[j].device)
				unit++;
		}

		/* Convert to a bhnd info record */
		bcma_erom_to_core_info(&core, i, unit, &buffer[i]);
	}

cleanup:
	if (!error) {
		*cores = buffer;
		*num_cores = count;
	} else {
		if (buffer != NULL)
			free(buffer, M_BHND);
	}

	/* Restore the initial position */
	bcma_erom_seek(sc, initial_offset);
	return (error);
}

static void
bcma_erom_free_core_table(bhnd_erom_t *erom, struct bhnd_core_info *cores)
{
	free(cores, M_BHND);
}

/**
 * Return the current read position.
 */
static bus_size_t
bcma_erom_tell(struct bcma_erom *erom)
{
	return (erom->offset);
}

/**
 * Seek to an absolute read position.
 */
static void
bcma_erom_seek(struct bcma_erom *erom, bus_size_t offset)
{
	erom->offset = offset;
}

/**
 * Read a 32-bit entry value from the EROM table without advancing the
 * read position.
 * 
 * @param erom EROM read state.
 * @param entry Will contain the read result on success.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero The read could not be completed.
 */
static int
bcma_erom_peek32(struct bcma_erom *erom, uint32_t *entry)
{
	if (erom->offset >= (BCMA_EROM_TABLE_SIZE - sizeof(uint32_t))) {
		EROM_LOG(erom, "BCMA EROM table missing terminating EOF\n");
		return (EINVAL);
	}

	*entry = bhnd_erom_io_read(erom->eio, erom->offset, 4);
	return (0);
}

/**
 * Read a 32-bit entry value from the EROM table.
 * 
 * @param erom EROM read state.
 * @param entry Will contain the read result on success.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero The read could not be completed.
 */
static int
bcma_erom_read32(struct bcma_erom *erom, uint32_t *entry)
{
	int error;

	if ((error = bcma_erom_peek32(erom, entry)) == 0)
		erom->offset += 4;

	return (error);
}

/**
 * Read and discard 32-bit entry value from the EROM table.
 * 
 * @param erom EROM read state.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero The read could not be completed.
 */
static int
bcma_erom_skip32(struct bcma_erom *erom)
{
	uint32_t	entry;

	return bcma_erom_read32(erom, &entry);
}

/**
 * Read and discard a core descriptor from the EROM table.
 * 
 * @param erom EROM read state.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero The read could not be completed.
 */
static int
bcma_erom_skip_core(struct bcma_erom *erom)
{
	struct bcma_erom_core core;
	return (bcma_erom_parse_core(erom, &core));
}

/**
 * Read and discard a master port descriptor from the EROM table.
 * 
 * @param erom EROM read state.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero The read could not be completed.
 */
static int
bcma_erom_skip_mport(struct bcma_erom *erom)
{
	struct bcma_erom_mport mp;
	return (bcma_erom_parse_mport(erom, &mp));
}

/**
 * Read and discard a port region descriptor from the EROM table.
 * 
 * @param erom EROM read state.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero The read could not be completed.
 */
static int
bcma_erom_skip_sport_region(struct bcma_erom *erom)
{
	struct bcma_erom_sport_region r;
	return (bcma_erom_parse_sport_region(erom, &r));
}

/**
 * Seek to the next entry matching the given EROM entry type.
 * 
 * @param erom EROM read state.
 * @param etype  One of BCMA_EROM_ENTRY_TYPE_CORE,
 * BCMA_EROM_ENTRY_TYPE_MPORT, or BCMA_EROM_ENTRY_TYPE_REGION.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero Reading or parsing the descriptor failed.
 */
static int
bcma_erom_seek_next(struct bcma_erom *erom, uint8_t etype)
{
	uint32_t			entry;
	int				error;

	/* Iterate until we hit an entry matching the requested type. */
	while (!(error = bcma_erom_peek32(erom, &entry))) {
		/* Handle EOF */
		if (entry == BCMA_EROM_TABLE_EOF)
			return (ENOENT);

		/* Invalid entry */
		if (!BCMA_EROM_GET_ATTR(entry, ENTRY_ISVALID))
			return (EINVAL);

		/* Entry type matches? */
		if (BCMA_EROM_GET_ATTR(entry, ENTRY_TYPE) == etype)
			return (0);

		/* Skip non-matching entry types. */
		switch (BCMA_EROM_GET_ATTR(entry, ENTRY_TYPE)) {
		case BCMA_EROM_ENTRY_TYPE_CORE:
			if ((error = bcma_erom_skip_core(erom)))
				return (error);

			break;

		case BCMA_EROM_ENTRY_TYPE_MPORT:
			if ((error = bcma_erom_skip_mport(erom)))
				return (error);

			break;
		
		case BCMA_EROM_ENTRY_TYPE_REGION:
			if ((error = bcma_erom_skip_sport_region(erom)))
				return (error);
			break;

		default:
			/* Unknown entry type! */
			return (EINVAL);	
		}
	}

	return (error);
}

/**
 * Return the read position to the start of the EROM table.
 * 
 * @param erom EROM read state.
 */
static void
bcma_erom_reset(struct bcma_erom *erom)
{
	erom->offset = 0;
}

/**
 * Seek to the first core entry matching @p desc.
 * 
 * @param erom EROM read state.
 * @param desc The core match descriptor.
 * @param[out] core On success, the matching core info. If the core info
 * is not desired, a NULL pointer may be provided.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached before @p index was
 * found.
 * @retval non-zero Reading or parsing failed.
 */
static int
bcma_erom_seek_matching_core(struct bcma_erom *sc,
    const struct bhnd_core_match *desc, struct bhnd_core_info *core)
{
	struct bhnd_core_match	 imatch;
	bus_size_t		 core_offset, next_offset;
	int			 error;

	/* Seek to table start. */
	bcma_erom_reset(sc);

	/* We can't determine a core's unit number during the initial scan. */
	imatch = *desc;
	imatch.m.match.core_unit = 0;

	/* Locate the first matching core */
	for (u_int i = 0; i < UINT_MAX; i++) {
		struct bcma_erom_core	ec;
		struct bhnd_core_info	ci;

		/* Seek to the next core */
		error = bcma_erom_seek_next(sc, BCMA_EROM_ENTRY_TYPE_CORE);
		if (error)
			return (error);

		/* Save the core offset */
		core_offset = bcma_erom_tell(sc);
	
		/* Parse the core */
		if ((error = bcma_erom_parse_core(sc, &ec)))
			return (error);

		bcma_erom_to_core_info(&ec, i, 0, &ci);

		/* Check for initial match */
		if (!bhnd_core_matches(&ci, &imatch))
			continue;

		/* Re-scan preceding cores to determine the unit number. */
		next_offset = bcma_erom_tell(sc);
		bcma_erom_reset(sc);
		for (u_int j = 0; j < i; j++) {
			/* Parse the core */
			error = bcma_erom_seek_next(sc,
			    BCMA_EROM_ENTRY_TYPE_CORE);
			if (error)
				return (error);
			
			if ((error = bcma_erom_parse_core(sc, &ec)))
				return (error);

			/* Bump the unit number? */
			if (ec.vendor == ci.vendor && ec.device == ci.device)
				ci.unit++;
		}

		/* Check for full match against now-valid unit number */
		if (!bhnd_core_matches(&ci, desc)) {
			/* Reposition to allow reading the next core */
			bcma_erom_seek(sc, next_offset);
			continue;
		}

		/* Found; seek to the core's initial offset and provide
		 * the core info to the caller */
		bcma_erom_seek(sc, core_offset);
		if (core != NULL)
			*core = ci;

		return (0);
	}

	/* Not found, or a parse error occured */
	return (error);
}

/**
 * Read the next core descriptor from the EROM table.
 * 
 * @param erom EROM read state.
 * @param[out] core On success, will be populated with the parsed core
 * descriptor data.
 * @retval 0 success
 * @retval ENOENT The end of the EROM table was reached.
 * @retval non-zero Reading or parsing the core descriptor failed.
 */
static int
bcma_erom_parse_core(struct bcma_erom *erom, struct bcma_erom_core *core)
{
	uint32_t	entry;
	int		error;

	/* Parse CoreDescA */
	if ((error = bcma_erom_read32(erom, &entry)))
		return (error);
	
	/* Handle EOF */
	if (entry == BCMA_EROM_TABLE_EOF)
		return (ENOENT);
	
	if (!BCMA_EROM_ENTRY_IS(entry, CORE)) {
		EROM_LOG(erom, "Unexpected EROM entry 0x%x (type=%s)\n",
                   entry, bcma_erom_entry_type_name(entry));
		
		return (EINVAL);
	}

	core->vendor = BCMA_EROM_GET_ATTR(entry, COREA_DESIGNER);
	core->device = BCMA_EROM_GET_ATTR(entry, COREA_ID);
	
	/* Parse CoreDescB */
	if ((error = bcma_erom_read32(erom, &entry)))
		return (error);

	if (!BCMA_EROM_ENTRY_IS(entry, CORE)) {
		return (EINVAL);
	}

	core->rev = BCMA_EROM_GET_ATTR(entry, COREB_REV);
	core->num_mport = BCMA_EROM_GET_ATTR(entry, COREB_NUM_MP);
	core->num_dport = BCMA_EROM_GET_ATTR(entry, COREB_NUM_DP);
	core->num_mwrap = BCMA_EROM_GET_ATTR(entry, COREB_NUM_WMP);
	core->num_swrap = BCMA_EROM_GET_ATTR(entry, COREB_NUM_WSP);

	return (0);
}

/**
 * Read the next master port descriptor from the EROM table.
 * 
 * @param erom EROM read state.
 * @param[out] mport On success, will be populated with the parsed
 * descriptor data.
 * @retval 0 success
 * @retval non-zero Reading or parsing the descriptor failed.
 */
static int
bcma_erom_parse_mport(struct bcma_erom *erom, struct bcma_erom_mport *mport)
{
	uint32_t	entry;
	int		error;

	/* Parse the master port descriptor */
	if ((error = bcma_erom_read32(erom, &entry)))
		return (error);
	
	if (!BCMA_EROM_ENTRY_IS(entry, MPORT))
		return (EINVAL);

	mport->port_vid = BCMA_EROM_GET_ATTR(entry, MPORT_ID);
	mport->port_num = BCMA_EROM_GET_ATTR(entry, MPORT_NUM);

	return (0);
}

/**
 * Read the next slave port region descriptor from the EROM table.
 * 
 * @param erom EROM read state.
 * @param[out] mport On success, will be populated with the parsed
 * descriptor data.
 * @retval 0 success
 * @retval ENOENT The end of the region descriptor table was reached.
 * @retval non-zero Reading or parsing the descriptor failed.
 */
static int
bcma_erom_parse_sport_region(struct bcma_erom *erom,
    struct bcma_erom_sport_region *region)
{
	uint32_t	entry;
	uint8_t		size_type;
	int		error;

	/* Peek at the region descriptor */
	if (bcma_erom_peek32(erom, &entry))
		return (EINVAL);

	/* A non-region entry signals the end of the region table */
	if (!BCMA_EROM_ENTRY_IS(entry, REGION)) {
		return (ENOENT);
	} else {
		bcma_erom_skip32(erom);
	}

	region->base_addr = BCMA_EROM_GET_ATTR(entry, REGION_BASE);
	region->region_type = BCMA_EROM_GET_ATTR(entry, REGION_TYPE);
	region->region_port = BCMA_EROM_GET_ATTR(entry, REGION_PORT);
	size_type = BCMA_EROM_GET_ATTR(entry, REGION_SIZE);

	/* If region address is 64-bit, fetch the high bits. */
	if (BCMA_EROM_GET_ATTR(entry, REGION_64BIT)) {
		if ((error = bcma_erom_read32(erom, &entry)))
			return (error);
		
		region->base_addr |= ((bhnd_addr_t) entry << 32);
	}

	/* Parse the region size; it's either encoded as the binary logarithm
	 * of the number of 4K pages (i.e. log2 n), or its encoded as a
	 * 32-bit/64-bit literal value directly following the current entry. */
	if (size_type == BCMA_EROM_REGION_SIZE_OTHER) {
		if ((error = bcma_erom_read32(erom, &entry)))
			return (error);

		region->size = BCMA_EROM_GET_ATTR(entry, RSIZE_VAL);

		if (BCMA_EROM_GET_ATTR(entry, RSIZE_64BIT)) {
			if ((error = bcma_erom_read32(erom, &entry)))
				return (error);
			region->size |= ((bhnd_size_t) entry << 32);
		}
	} else {
		region->size = BCMA_EROM_REGION_SIZE_BASE << size_type;
	}

	/* Verify that addr+size does not overflow. */
	if (region->size != 0 &&
	    BHND_ADDR_MAX - (region->size - 1) < region->base_addr)
	{
		EROM_LOG(erom, "%s%u: invalid address map %llx:%llx\n",
		    bcma_erom_entry_type_name(region->region_type),
		    region->region_port,
		    (unsigned long long) region->base_addr,
		    (unsigned long long) region->size);

		return (EINVAL);
	}

	return (0);
}

/**
 * Convert a bcma_erom_core record to its bhnd_core_info representation.
 * 
 * @param core EROM core record to convert.
 * @param core_idx The core index of @p core.
 * @param core_unit The core unit of @p core.
 * @param[out] info The populated bhnd_core_info representation.
 */
static void
bcma_erom_to_core_info(const struct bcma_erom_core *core, u_int core_idx,
    int core_unit, struct bhnd_core_info *info)
{
	info->vendor = core->vendor;
	info->device = core->device;
	info->hwrev = core->rev;
	info->core_idx = core_idx;
	info->unit = core_unit;
}

/**
 * Map an EROM region type to its corresponding port type.
 * 
 * @param region_type Region type value.
 * @param[out] port_type On success, the corresponding port type.
 */
static int
bcma_erom_region_to_port_type(struct bcma_erom *erom, uint8_t region_type,
    bhnd_port_type *port_type)
{
	switch (region_type) {
	case BCMA_EROM_REGION_TYPE_DEVICE:
		*port_type = BHND_PORT_DEVICE;
		return (0);
	case BCMA_EROM_REGION_TYPE_BRIDGE:
		*port_type = BHND_PORT_BRIDGE;
		return (0);
	case BCMA_EROM_REGION_TYPE_MWRAP:
	case BCMA_EROM_REGION_TYPE_SWRAP:
		*port_type = BHND_PORT_AGENT;
		return (0);
	default:
		EROM_LOG(erom, "unsupported region type %hhx\n",
			region_type);
		return (EINVAL);
	}
}

/**
 * Register all MMIO region descriptors for the given slave port.
 * 
 * @param erom EROM read state.
 * @param corecfg Core info to be populated with the scanned port regions.
 * @param port_num Port index for which regions will be parsed.
 * @param region_type The region type to be parsed.
 * @param[out] offset The offset at which to perform parsing. On success, this
 * will be updated to point to the next EROM table entry.
 */
static int 
bcma_erom_corecfg_fill_port_regions(struct bcma_erom *erom,
    struct bcma_corecfg *corecfg, bcma_pid_t port_num,
    uint8_t region_type)
{
	struct bcma_sport	*sport;
	struct bcma_sport_list	*sports;
	bus_size_t		 entry_offset;
	int			 error;
	bhnd_port_type		 port_type;

	error = 0;

	/* Determine the port type for this region type. */
	error = bcma_erom_region_to_port_type(erom, region_type, &port_type);
	if (error)
		return (error);

	/* Fetch the list to be populated */
	sports = bcma_corecfg_get_port_list(corecfg, port_type);
	
	/* Allocate a new port descriptor */
	sport = bcma_alloc_sport(port_num, port_type);
	if (sport == NULL)
		return (ENOMEM);

	/* Read all address regions defined for this port */
	for (bcma_rmid_t region_num = 0;; region_num++) {
		struct bcma_map			*map;
		struct bcma_erom_sport_region	 spr;

		/* No valid port definition should come anywhere near
		 * BCMA_RMID_MAX. */
		if (region_num == BCMA_RMID_MAX) {
			EROM_LOG(erom, "core%u %s%u: region count reached "
			    "upper limit of %u\n",
			    corecfg->core_info.core_idx,
			    bhnd_port_type_name(port_type),
			    port_num, BCMA_RMID_MAX);

			error = EINVAL;
			goto cleanup;
		}

		/* Parse the next region entry. */
		entry_offset = bcma_erom_tell(erom);
		error = bcma_erom_parse_sport_region(erom, &spr);
		if (error && error != ENOENT) {
			EROM_LOG(erom, "core%u %s%u.%u: invalid slave port "
			    "address region\n",
			    corecfg->core_info.core_idx,
			    bhnd_port_type_name(port_type),
			    port_num, region_num);
			goto cleanup;
		}

		/* ENOENT signals no further region entries */
		if (error == ENOENT) {
			/* No further entries */
			error = 0;
			break;
		} 
		
		/* A region or type mismatch also signals no further region
		 * entries */
		if (spr.region_port != port_num ||
		    spr.region_type != region_type)
		{
			/* We don't want to consume this entry */
			bcma_erom_seek(erom, entry_offset);

			error = 0;
			goto cleanup;
		}

		/*
		 * Create the map entry. 
		 */
		map = malloc(sizeof(struct bcma_map), M_BHND, M_NOWAIT);
		if (map == NULL) {
			error = ENOMEM;
			goto cleanup;
		}

		map->m_region_num = region_num;
		map->m_base = spr.base_addr;
		map->m_size = spr.size;
		map->m_rid = -1;

		/* Add the region map to the port */
		STAILQ_INSERT_TAIL(&sport->sp_maps, map, m_link);
		sport->sp_num_maps++;
	}

cleanup:
	/* Append the new port descriptor on success, or deallocate the
	 * partially parsed descriptor on failure. */
	if (error == 0) {
		STAILQ_INSERT_TAIL(sports, sport, sp_link);
	} else if (sport != NULL) {
		bcma_free_sport(sport);
	}

	return error;
}

/**
 * Parse the next core entry from the EROM table and produce a bcma_corecfg
 * to be owned by the caller.
 * 
 * @param erom A bcma EROM instance.
 * @param[out] result On success, the core's device info. The caller inherits
 * ownership of this allocation.
 * 
 * @return If successful, returns 0. If the end of the EROM table is hit,
 * ENOENT will be returned. On error, returns a non-zero error value.
 */
int
bcma_erom_next_corecfg(struct bcma_erom *erom, struct bcma_corecfg **result)
{
	struct bcma_corecfg	*cfg;
	struct bcma_erom_core	 core;
	uint8_t			 first_region_type;
	bus_size_t		 initial_offset;
	u_int			 core_index;
	int			 core_unit;
	int			 error;

	cfg = NULL;
	initial_offset = bcma_erom_tell(erom);

	/* Parse the next core entry */
	if ((error = bcma_erom_parse_core(erom, &core)))
		return (error);

	/* Determine the core's index and unit numbers */
	bcma_erom_reset(erom);
	core_unit = 0;
	core_index = 0;
	for (; bcma_erom_tell(erom) != initial_offset; core_index++) {
		struct bcma_erom_core prev_core;

		/* Parse next core */
		error = bcma_erom_seek_next(erom, BCMA_EROM_ENTRY_TYPE_CORE);
		if (error)
			return (error);

		if ((error = bcma_erom_parse_core(erom, &prev_core)))
			return (error);

		/* Is earlier unit? */
		if (core.vendor == prev_core.vendor &&
		    core.device == prev_core.device)
		{
			core_unit++;
		}

		/* Seek to next core */
		error = bcma_erom_seek_next(erom, BCMA_EROM_ENTRY_TYPE_CORE);
		if (error)
			return (error);
	}

	/* We already parsed the core descriptor */
	if ((error = bcma_erom_skip_core(erom)))
		return (error);

	/* Allocate our corecfg */
	cfg = bcma_alloc_corecfg(core_index, core_unit, core.vendor,
	    core.device, core.rev);
	if (cfg == NULL)
		return (ENOMEM);
	
	/* These are 5-bit values in the EROM table, and should never be able
	 * to overflow BCMA_PID_MAX. */
	KASSERT(core.num_mport <= BCMA_PID_MAX, ("unsupported mport count"));
	KASSERT(core.num_dport <= BCMA_PID_MAX, ("unsupported dport count"));
	KASSERT(core.num_mwrap + core.num_swrap <= BCMA_PID_MAX,
	    ("unsupported wport count"));

	if (bootverbose) {
		EROM_LOG(erom, 
		    "core%u: %s %s (cid=%hx, rev=%hu, unit=%d)\n",
		    core_index,
		    bhnd_vendor_name(core.vendor),
		    bhnd_find_core_name(core.vendor, core.device), 
		    core.device, core.rev, core_unit);
	}

	cfg->num_master_ports = core.num_mport;
	cfg->num_dev_ports = 0;		/* determined below */
	cfg->num_bridge_ports = 0;	/* determined blow */
	cfg->num_wrapper_ports = core.num_mwrap + core.num_swrap;

	/* Parse Master Port Descriptors */
	for (uint8_t i = 0; i < core.num_mport; i++) {
		struct bcma_mport	*mport;
		struct bcma_erom_mport	 mpd;
	
		/* Parse the master port descriptor */
		error = bcma_erom_parse_mport(erom, &mpd);
		if (error)
			goto failed;

		/* Initialize a new bus mport structure */
		mport = malloc(sizeof(struct bcma_mport), M_BHND, M_NOWAIT);
		if (mport == NULL) {
			error = ENOMEM;
			goto failed;
		}
		
		mport->mp_vid = mpd.port_vid;
		mport->mp_num = mpd.port_num;

		/* Update dinfo */
		STAILQ_INSERT_TAIL(&cfg->master_ports, mport, mp_link);
	}
	

	/*
	 * Determine whether this is a bridge device; if so, we can
	 * expect the first sequence of address region descriptors to
	 * be of EROM_REGION_TYPE_BRIDGE instead of
	 * BCMA_EROM_REGION_TYPE_DEVICE.
	 * 
	 * It's unclear whether this is the correct mechanism by which we
	 * should detect/handle bridge devices, but this approach matches
	 * that of (some of) Broadcom's published drivers.
	 */
	if (core.num_dport > 0) {
		uint32_t entry;

		if ((error = bcma_erom_peek32(erom, &entry)))
			goto failed;

		if (BCMA_EROM_ENTRY_IS(entry, REGION) && 
		    BCMA_EROM_GET_ATTR(entry, REGION_TYPE) == BCMA_EROM_REGION_TYPE_BRIDGE)
		{
			first_region_type = BCMA_EROM_REGION_TYPE_BRIDGE;
			cfg->num_dev_ports = 0;
			cfg->num_bridge_ports = core.num_dport;
		} else {
			first_region_type = BCMA_EROM_REGION_TYPE_DEVICE;
			cfg->num_dev_ports = core.num_dport;
			cfg->num_bridge_ports = 0;
		}
	}
	
	/* Device/bridge port descriptors */
	for (uint8_t sp_num = 0; sp_num < core.num_dport; sp_num++) {
		error = bcma_erom_corecfg_fill_port_regions(erom, cfg, sp_num,
		    first_region_type);

		if (error)
			goto failed;
	}

	/* Wrapper (aka device management) descriptors (for master ports). */
	for (uint8_t sp_num = 0; sp_num < core.num_mwrap; sp_num++) {
		error = bcma_erom_corecfg_fill_port_regions(erom, cfg, sp_num,
		    BCMA_EROM_REGION_TYPE_MWRAP);

		if (error)
			goto failed;
	}

	
	/* Wrapper (aka device management) descriptors (for slave ports). */	
	for (uint8_t i = 0; i < core.num_swrap; i++) {
		/* Slave wrapper ports are not numbered distinctly from master
		 * wrapper ports. */

		/* 
		 * Broadcom DDR1/DDR2 Memory Controller
		 * (cid=82e, rev=1, unit=0, d/mw/sw = 2/0/1 ) ->
		 * bhnd0: erom[0xdc]: core6 agent0.0: mismatch got: 0x1 (0x2)
		 *
		 * ARM BP135 AMBA3 AXI to APB Bridge
		 * (cid=135, rev=0, unit=0, d/mw/sw = 1/0/1 ) ->
		 * bhnd0: erom[0x124]: core9 agent1.0: mismatch got: 0x0 (0x2)
		 *
		 * core.num_mwrap
		 * ===>
		 * (core.num_mwrap > 0) ?
		 *           core.num_mwrap :
		 *           ((core.vendor == BHND_MFGID_BCM) ? 1 : 0)
		 */
		uint8_t sp_num;
		sp_num = (core.num_mwrap > 0) ?
				core.num_mwrap :
				((core.vendor == BHND_MFGID_BCM) ? 1 : 0) + i;
		error = bcma_erom_corecfg_fill_port_regions(erom, cfg, sp_num,
		    BCMA_EROM_REGION_TYPE_SWRAP);

		if (error)
			goto failed;
	}

	/*
	 * Seek to the next core entry (if any), skipping any dangling/invalid
	 * region entries.
	 * 
	 * On the BCM4706, the EROM entry for the memory controller core
	 * (0x4bf/0x52E) contains a dangling/unused slave wrapper port region
	 * descriptor.
	 */
	if ((error = bcma_erom_seek_next(erom, BCMA_EROM_ENTRY_TYPE_CORE))) {
		if (error != ENOENT)
			goto failed;
	}

	*result = cfg;
	return (0);
	
failed:
	if (cfg != NULL)
		bcma_free_corecfg(cfg);

	return error;
}

static int
bcma_erom_dump(bhnd_erom_t *erom)
{
	struct bcma_erom	*sc;
	uint32_t		entry;
	int			error;

	sc = (struct bcma_erom *)erom;

	bcma_erom_reset(sc);

	while (!(error = bcma_erom_read32(sc, &entry))) {
		/* Handle EOF */
		if (entry == BCMA_EROM_TABLE_EOF) {
			EROM_LOG(sc, "EOF\n");
			return (0);
		}

		/* Invalid entry */
		if (!BCMA_EROM_GET_ATTR(entry, ENTRY_ISVALID)) {
			EROM_LOG(sc, "invalid EROM entry %#x\n", entry);
			return (EINVAL);
		}

		switch (BCMA_EROM_GET_ATTR(entry, ENTRY_TYPE)) {
		case BCMA_EROM_ENTRY_TYPE_CORE: {
			/* CoreDescA */
			EROM_LOG(sc, "coreA (0x%x)\n", entry);
			EROM_LOG(sc, "\tdesigner:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREA_DESIGNER));
			EROM_LOG(sc, "\tid:\t\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREA_ID));
			EROM_LOG(sc, "\tclass:\t\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREA_CLASS));

			/* CoreDescB */
			if ((error = bcma_erom_read32(sc, &entry))) {
				EROM_LOG(sc, "error reading CoreDescB: %d\n",
				    error);
				return (error);
			}

			if (!BCMA_EROM_ENTRY_IS(entry, CORE)) {
				EROM_LOG(sc, "invalid core descriptor; found "
				    "unexpected entry %#x (type=%s)\n",
				    entry, bcma_erom_entry_type_name(entry));
				return (EINVAL);
			}

			EROM_LOG(sc, "coreB (0x%x)\n", entry);
			EROM_LOG(sc, "\trev:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREB_REV));
			EROM_LOG(sc, "\tnummp:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREB_NUM_MP));
			EROM_LOG(sc, "\tnumdp:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREB_NUM_DP));
			EROM_LOG(sc, "\tnumwmp:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREB_NUM_WMP));
			EROM_LOG(sc, "\tnumwsp:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, COREB_NUM_WMP));

			break;
		}
		case BCMA_EROM_ENTRY_TYPE_MPORT:
			EROM_LOG(sc, "\tmport 0x%x\n", entry);
			EROM_LOG(sc, "\t\tport:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, MPORT_NUM));
			EROM_LOG(sc, "\t\tid:\t\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, MPORT_ID));
			break;

		case BCMA_EROM_ENTRY_TYPE_REGION: {
			bool	addr64;
			uint8_t	size_type;

			addr64 = (BCMA_EROM_GET_ATTR(entry, REGION_64BIT) != 0);
			size_type = BCMA_EROM_GET_ATTR(entry, REGION_SIZE);

			EROM_LOG(sc, "\tregion 0x%x:\n", entry);
			EROM_LOG(sc, "\t\t%s:\t0x%x\n",
			    addr64 ? "baselo" : "base",
			    BCMA_EROM_GET_ATTR(entry, REGION_BASE));
			EROM_LOG(sc, "\t\tport:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, REGION_PORT));
			EROM_LOG(sc, "\t\ttype:\t0x%x\n",
			    BCMA_EROM_GET_ATTR(entry, REGION_TYPE));
			EROM_LOG(sc, "\t\tsztype:\t0x%hhx\n", size_type);

			/* Read the base address high bits */
			if (addr64) {
				if ((error = bcma_erom_read32(sc, &entry))) {
					EROM_LOG(sc, "error reading region "
					    "base address high bits %d\n",
					    error);
					return (error);
				}

				EROM_LOG(sc, "\t\tbasehi:\t0x%x\n", entry);
			}

			/* Read extended size descriptor */
			if (size_type == BCMA_EROM_REGION_SIZE_OTHER) {
				bool size64;

				if ((error = bcma_erom_read32(sc, &entry))) {
					EROM_LOG(sc, "error reading region "
					    "size descriptor %d\n",
					    error);
					return (error);
				}

				if (BCMA_EROM_GET_ATTR(entry, RSIZE_64BIT))
					size64 = true;
				else
					size64 = false;

				EROM_LOG(sc, "\t\t%s:\t0x%x\n",
				    size64 ? "sizelo" : "size",
				    BCMA_EROM_GET_ATTR(entry, RSIZE_VAL));

				if (size64) {
					error = bcma_erom_read32(sc, &entry);
					if (error) {
						EROM_LOG(sc, "error reading "
						    "region size high bits: "
						    "%d\n", error);
						return (error);
					}

					EROM_LOG(sc, "\t\tsizehi:\t0x%x\n",
					    entry);
				}
			}
			break;
		}

		default:
			EROM_LOG(sc, "unknown EROM entry 0x%x (type=%s)\n",
			    entry, bcma_erom_entry_type_name(entry));
			return (EINVAL);
		}
	}

	if (error == ENOENT)
		EROM_LOG(sc, "BCMA EROM table missing terminating EOF\n");
	else if (error)
		EROM_LOG(sc, "EROM read failed: %d\n", error);

	return (error);
}

static kobj_method_t bcma_erom_methods[] = {
	KOBJMETHOD(bhnd_erom_probe,		bcma_erom_probe),
	KOBJMETHOD(bhnd_erom_init,		bcma_erom_init),
	KOBJMETHOD(bhnd_erom_fini,		bcma_erom_fini),
	KOBJMETHOD(bhnd_erom_get_core_table,	bcma_erom_get_core_table),
	KOBJMETHOD(bhnd_erom_free_core_table,	bcma_erom_free_core_table),
	KOBJMETHOD(bhnd_erom_lookup_core,	bcma_erom_lookup_core),
	KOBJMETHOD(bhnd_erom_lookup_core_addr,	bcma_erom_lookup_core_addr),
	KOBJMETHOD(bhnd_erom_dump,		bcma_erom_dump),

	KOBJMETHOD_END
};

BHND_EROM_DEFINE_CLASS(bcma_erom, bcma_erom_parser, bcma_erom_methods, sizeof(struct bcma_erom));
