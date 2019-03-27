/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
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
#include <sys/kobj.h>
  
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhndreg.h>
#include <dev/bhnd/bhndvar.h>

#include <dev/bhnd/bhnd_erom.h>
#include <dev/bhnd/bhnd_eromvar.h>

#include <dev/bhnd/cores/chipc/chipcreg.h>

static int	bhnd_erom_iores_map(struct bhnd_erom_io *eio, bhnd_addr_t addr,
		    bhnd_size_t size);
static int	bhnd_erom_iores_tell(struct bhnd_erom_io *eio,
		    bhnd_addr_t *addr, bhnd_size_t *size);
static uint32_t	bhnd_erom_iores_read(struct bhnd_erom_io *eio,
		    bhnd_size_t offset, u_int width);
static void	bhnd_erom_iores_fini(struct bhnd_erom_io *eio);

static int	bhnd_erom_iobus_map(struct bhnd_erom_io *eio, bhnd_addr_t addr,
		    bhnd_size_t size);
static int	bhnd_erom_iobus_tell(struct bhnd_erom_io *eio,
		    bhnd_addr_t *addr, bhnd_size_t *size);
static uint32_t	bhnd_erom_iobus_read(struct bhnd_erom_io *eio,
		    bhnd_size_t offset, u_int width);

/**
 * An implementation of bhnd_erom_io that manages mappings via
 * bhnd_alloc_resource() and bhnd_release_resource().
 */
struct bhnd_erom_iores {
	struct bhnd_erom_io	 eio;
	device_t		 owner;		/**< device from which we'll allocate resources */
	int			 owner_rid;	/**< rid to use when allocating new mappings */
	struct bhnd_resource	*mapped;	/**< current mapping, or NULL */
	int			 mapped_rid;	/**< resource ID of current mapping, or -1 */
};

/**
 * Fetch the device enumeration parser class from all bhnd(4)-compatible drivers
 * registered for @p bus_devclass, probe @p eio for supporting parser classes,
 * and return the best available supporting enumeration parser class.
 * 
 * @param	bus_devclass	The bus device class to be queried for
 *				bhnd(4)-compatible drivers.
 * @param	eio		An erom bus I/O instance, configured with a
 *				mapping of the first bus core.
 * @param	hint		Identification hint used to identify the device.
 *				If the chipset supports standard chip
 *				identification registers within the first core,
 *				this parameter should be NULL.
 * @param[out]	cid		On success, the probed chip identifier.
 * 
 * @retval non-NULL	on success, the best available EROM class.
 * @retval NULL		if no erom class returned a successful probe result for
 *			@p eio.
 */
bhnd_erom_class_t *
bhnd_erom_probe_driver_classes(devclass_t bus_devclass,
    struct bhnd_erom_io *eio, const struct bhnd_chipid *hint,
    struct bhnd_chipid *cid)
{
	driver_t		**drivers;
	int			 drv_count;
	bhnd_erom_class_t	*erom_cls;
	int			 error, prio, result;

	erom_cls = NULL;
	prio = 0;

	/* Fetch all available drivers */
	error = devclass_get_drivers(bus_devclass, &drivers, &drv_count);
	if (error) {
		printf("error fetching bhnd(4) drivers for %s: %d\n",
		    devclass_get_name(bus_devclass), error);
		return (NULL);
	}

	/* Enumerate the drivers looking for the best available EROM class */
	for (int i = 0; i < drv_count; i++) {
		struct bhnd_chipid	 pcid;
		bhnd_erom_class_t	*cls;

		/* The default implementation of BHND_BUS_GET_EROM_CLASS()
		 * returns NULL if unimplemented; this should always be safe
		 * to call on arbitrary drivers */
		cls = bhnd_driver_get_erom_class(drivers[i]);
		if (cls == NULL)
			continue;

		kobj_class_compile(cls);

		/* Probe the bus */
		result = bhnd_erom_probe(cls, eio, hint, &pcid);

		/* The parser did not match if an error was returned */
		if (result > 0)
			continue;
		
		/* Check for a new highest priority match */
		if (erom_cls == NULL || result > prio) {
			prio = result;

			*cid = pcid;
			erom_cls = cls;
		}

		/* Terminate immediately on BUS_PROBE_SPECIFIC */
		if (result == BUS_PROBE_SPECIFIC)
			break;
	}

	free(drivers, M_TEMP);
	return (erom_cls);
}

/**
 * Allocate and return a new device enumeration table parser.
 * 
 * @param cls		The parser class for which an instance will be
 *			allocated.
 * @param eio		The bus I/O callbacks to use when reading the device
 *			enumeration table.
 * @param cid		The device's chip identifier.
 *
 * @retval non-NULL	success
 * @retval NULL		if an error occured allocating or initializing the
 *			EROM parser.
 */
bhnd_erom_t *
bhnd_erom_alloc(bhnd_erom_class_t *cls, const struct bhnd_chipid *cid,
    struct bhnd_erom_io *eio)
{
	bhnd_erom_t	*erom;
	int		 error;

	erom = (bhnd_erom_t *)kobj_create((kobj_class_t)cls, M_BHND,
	    M_WAITOK|M_ZERO);

	if ((error = BHND_EROM_INIT(erom, cid, eio))) {
		printf("error initializing %s parser at %#jx: %d\n", cls->name,
		    (uintmax_t)cid->enum_addr, error);

		kobj_delete((kobj_t)erom, M_BHND);
		return (NULL);
	}

	return (erom);
}

/**
 * Perform static initialization of a device enumeration table parser.
 * 
 * This may be used to initialize a caller-allocated erom instance state
 * during early boot, prior to malloc availability.
 * 
 * @param cls		The parser class for which an instance will be
 *			allocated.
 * @param erom		The erom parser instance to initialize.
 * @param esize		The total available number of bytes allocated for
 *			@p erom. If this is less than is required by @p cls,
 *			ENOMEM will be returned.
 * @param cid		The device's chip identifier.
 * @param eio		The bus I/O callbacks to use when reading the device
 *			enumeration table.
 *
 * @retval 0		success
 * @retval ENOMEM	if @p esize is smaller than required by @p cls.
 * @retval non-zero	if an error occurs initializing the EROM parser,
 *			a regular unix error code will be returned.
 */
int
bhnd_erom_init_static(bhnd_erom_class_t *cls, bhnd_erom_t *erom, size_t esize,
    const struct bhnd_chipid *cid, struct bhnd_erom_io *eio)
{
	kobj_class_t	kcls;

	kcls = (kobj_class_t)cls;

	/* Verify allocation size */
	if (kcls->size > esize)
		return (ENOMEM);

	/* Perform instance initialization */
	kobj_init_static((kobj_t)erom, kcls);
	return (BHND_EROM_INIT(erom, cid, eio)); 
}

/**
 * Release any resources held by a @p erom parser previously
 * initialized via bhnd_erom_init_static().
 * 
 * @param	erom	An erom parser instance previously initialized via
 *			bhnd_erom_init_static().
 */
void
bhnd_erom_fini_static(bhnd_erom_t *erom)
{
	return (BHND_EROM_FINI(erom));
}

/**
 * Release all resources held by a @p erom parser previously
 * allocated via bhnd_erom_alloc().
 * 
 * @param	erom	An erom parser instance previously allocated via
 *			bhnd_erom_alloc().
 */
void
bhnd_erom_free(bhnd_erom_t *erom)
{
	BHND_EROM_FINI(erom);
	kobj_delete((kobj_t)erom, M_BHND);
}

/**
 * Read the chip identification registers mapped by @p eio, popuating @p cid
 * with the parsed result
 * 
 * @param	eio		A bus I/O instance, configured with a mapping
 *				of the ChipCommon core.
 * @param[out]	cid		On success, the parsed chip identification.
 *
 * @warning
 * On early siba(4) devices, the ChipCommon core does not provide
 * a valid CHIPC_ID_NUMCORE field. On these ChipCommon revisions
 * (see CHIPC_NCORES_MIN_HWREV()), this function will parse and return
 * an invalid `ncores` value.
 */
int
bhnd_erom_read_chipid(struct bhnd_erom_io *eio, struct bhnd_chipid *cid)
{
	bhnd_addr_t	cc_addr;
	bhnd_size_t	cc_size;
	uint32_t	idreg, cc_caps;
	int		error;

	/* Fetch ChipCommon address */
	if ((error = bhnd_erom_io_tell(eio, &cc_addr, &cc_size)))
		return (error);

	/* Read chip identifier */
	idreg = bhnd_erom_io_read(eio, CHIPC_ID, 4);

	/* Extract the basic chip info */
	cid->chip_id = CHIPC_GET_BITS(idreg, CHIPC_ID_CHIP);
	cid->chip_pkg = CHIPC_GET_BITS(idreg, CHIPC_ID_PKG);
	cid->chip_rev = CHIPC_GET_BITS(idreg, CHIPC_ID_REV);
	cid->chip_type = CHIPC_GET_BITS(idreg, CHIPC_ID_BUS);
	cid->ncores = CHIPC_GET_BITS(idreg, CHIPC_ID_NUMCORE);

	/* Populate EROM address */
	if (BHND_CHIPTYPE_HAS_EROM(cid->chip_type)) {
		cid->enum_addr = bhnd_erom_io_read(eio, CHIPC_EROMPTR, 4);
	} else {
		cid->enum_addr = cc_addr;
	}

	/* Populate capability flags */
	cc_caps = bhnd_erom_io_read(eio, CHIPC_CAPABILITIES, 4);
	cid->chip_caps = 0x0;

	if (cc_caps & CHIPC_CAP_BKPLN64)
		cid->chip_caps |= BHND_CAP_BP64;

	if (cc_caps & CHIPC_CAP_PMU)
		cid->chip_caps |= BHND_CAP_PMU;

	return (0);
}


/**
 * Attempt to map @p size bytes at @p addr, replacing any existing
 * @p eio mapping.
 * 
 * @param eio	I/O instance state.
 * @param addr	The address to be mapped.
 * @param size	The number of bytes to be mapped at @p addr.
 * 
 * @retval 0		success
 * @retval non-zero	if mapping @p addr otherwise fails, a regular
 *			unix error code should be returned.
 */
int
bhnd_erom_io_map(struct bhnd_erom_io *eio, bhnd_addr_t addr, bhnd_size_t size)
{
	return (eio->map(eio, addr, size));
}

/**
 * Return the address range mapped by @p eio, if any.
 * 
 * @param	eio	I/O instance state.
 * @param[out]	addr	The address mapped by @p eio.
 * @param[out]	size	The number of bytes mapped at @p addr.
 * 
 * @retval	0	success
 * @retval	ENXIO	if @p eio has no mapping.
 */
int
bhnd_erom_io_tell(struct bhnd_erom_io *eio, bhnd_addr_t *addr,
    bhnd_size_t *size)
{
	return (eio->tell(eio, addr, size));
}

/**
 * Read a 1, 2, or 4 byte data item from @p eio, at the given @p offset
 * relative to @p eio's current mapping.
 * 
 * @param eio		erom I/O callbacks
 * @param offset	read offset.
 * @param width		item width (1, 2, or 4 bytes).
 */
uint32_t
bhnd_erom_io_read(struct bhnd_erom_io *eio, bhnd_size_t offset, u_int width)
{
	return (eio->read(eio, offset, width));
}

/**
 * Free all resources held by @p eio.
 */
void
bhnd_erom_io_fini(struct bhnd_erom_io *eio)
{
	if (eio->fini != NULL)
		return (eio->fini(eio));
}

/**
 * Allocate, initialize, and return a new I/O instance that will perform
 * mapping by allocating SYS_RES_MEMORY resources from @p dev using @p rid.
 * 
 * @param dev	The device to pass to bhnd_alloc_resource() and
 *		bhnd_release_resource() functions.
 * @param rid	The resource ID to be used when allocating memory resources.
 */
struct bhnd_erom_io *
bhnd_erom_iores_new(device_t dev, int rid)
{
	struct bhnd_erom_iores	*iores;

	iores = malloc(sizeof(*iores), M_BHND, M_WAITOK | M_ZERO);
	iores->eio.map = bhnd_erom_iores_map;
	iores->eio.tell = bhnd_erom_iores_tell;
	iores->eio.read = bhnd_erom_iores_read;
	iores->eio.fini = bhnd_erom_iores_fini;

	iores->owner = dev;
	iores->owner_rid = rid;
	iores->mapped = NULL;
	iores->mapped_rid = -1;

	return (&iores->eio);
}

static int
bhnd_erom_iores_map(struct bhnd_erom_io *eio, bhnd_addr_t addr,
    bhnd_size_t size)
{
	struct bhnd_erom_iores *iores;

	iores = (struct bhnd_erom_iores *)eio;

	/* Sanity check the addr/size */
	if (size == 0)
		return (EINVAL);

	if (BHND_ADDR_MAX - size < addr)
		return (EINVAL);	/* would overflow */

	/* Check for an existing mapping */
	if (iores->mapped) {
		/* If already mapped, nothing else to do */
		if (rman_get_start(iores->mapped->res) == addr &&
		    rman_get_size(iores->mapped->res) == size)
		{
			return (0);
		}

		/* Otherwise, we need to drop the existing mapping */
		bhnd_release_resource(iores->owner, SYS_RES_MEMORY,
		    iores->mapped_rid, iores->mapped);
		iores->mapped = NULL;
		iores->mapped_rid = -1;
	}

	/* Try to allocate the new mapping */
	iores->mapped_rid = iores->owner_rid;
	iores->mapped = bhnd_alloc_resource(iores->owner, SYS_RES_MEMORY,
	    &iores->mapped_rid, addr, addr+size-1, size,
	    RF_ACTIVE|RF_SHAREABLE);
	if (iores->mapped == NULL) {
		iores->mapped_rid = -1;
		return (ENXIO);
	}

	return (0);
}

static int
bhnd_erom_iores_tell(struct bhnd_erom_io *eio, bhnd_addr_t *addr,
    bhnd_size_t *size)
{
	struct bhnd_erom_iores *iores = (struct bhnd_erom_iores *)eio;

	if (iores->mapped == NULL)
		return (ENXIO);

	*addr = rman_get_start(iores->mapped->res);
	*size = rman_get_size(iores->mapped->res);

	return (0);
}

static uint32_t
bhnd_erom_iores_read(struct bhnd_erom_io *eio, bhnd_size_t offset, u_int width)
{
	struct bhnd_erom_iores *iores = (struct bhnd_erom_iores *)eio;

	if (iores->mapped == NULL)
		panic("read with invalid mapping");

	switch (width) {
	case 1:
		return (bhnd_bus_read_1(iores->mapped, offset));
	case 2:
		return (bhnd_bus_read_2(iores->mapped, offset));
	case 4:
		return (bhnd_bus_read_4(iores->mapped, offset));
	default:
		panic("invalid width %u", width);
	}
}

static void
bhnd_erom_iores_fini(struct bhnd_erom_io *eio)
{
	struct bhnd_erom_iores *iores = (struct bhnd_erom_iores *)eio;

	/* Release any mapping */
	if (iores->mapped) {
		bhnd_release_resource(iores->owner, SYS_RES_MEMORY,
		    iores->mapped_rid, iores->mapped);
		iores->mapped = NULL;
		iores->mapped_rid = -1;
	}

	free(eio, M_BHND);
}

/**
 * Initialize an I/O instance that will perform mapping directly from the
 * given bus space tag and handle.
 * 
 * @param iobus	The I/O instance to be initialized.
 * @param addr	The base address mapped by @p bsh.
 * @param size	The total size mapped by @p bsh.
 * @param bst	Bus space tag for @p bsh.
 * @param bsh	Bus space handle mapping the full bus enumeration space.
 * 
 * @retval 0		success
 * @retval non-zero	if initializing @p iobus otherwise fails, a regular
 *			unix error code will be returned.
 */
int
bhnd_erom_iobus_init(struct bhnd_erom_iobus *iobus, bhnd_addr_t addr,
    bhnd_size_t size, bus_space_tag_t bst, bus_space_handle_t bsh)
{
	iobus->eio.map = bhnd_erom_iobus_map;
	iobus->eio.tell = bhnd_erom_iobus_tell;
	iobus->eio.read = bhnd_erom_iobus_read;
	iobus->eio.fini = NULL;

	iobus->addr = addr;
	iobus->size = size;
	iobus->bst = bst;
	iobus->bsh = bsh;
	iobus->mapped = false;

	return (0);
}

static int
bhnd_erom_iobus_map(struct bhnd_erom_io *eio, bhnd_addr_t addr,
    bhnd_size_t size)
{
	struct bhnd_erom_iobus *iobus = (struct bhnd_erom_iobus *)eio;

	/* Sanity check the addr/size */
	if (size == 0)
		return (EINVAL);

	/* addr+size must not overflow */
	if (BHND_ADDR_MAX - size < addr)
		return (EINVAL);

	/* addr/size must fit within our bus tag's mapping */
	if (addr < iobus->addr || size > iobus->size)
		return (ENXIO);

	if (iobus->size - (addr - iobus->addr) < size)
		return (ENXIO);

	/* The new addr offset and size must be representible as a bus_size_t */
	if ((addr - iobus->addr) > BUS_SPACE_MAXSIZE)
		return (ENXIO);

	if (size > BUS_SPACE_MAXSIZE)
		return (ENXIO);

	iobus->offset = addr - iobus->addr;
	iobus->limit = size;
	iobus->mapped = true;

	return (0);
}

static int
bhnd_erom_iobus_tell(struct bhnd_erom_io *eio, bhnd_addr_t *addr,
    bhnd_size_t *size)
{
	struct bhnd_erom_iobus *iobus = (struct bhnd_erom_iobus *)eio;

	if (!iobus->mapped)
		return (ENXIO);

	*addr = iobus->addr + iobus->offset;
	*size = iobus->limit;

	return (0);
}

static uint32_t
bhnd_erom_iobus_read(struct bhnd_erom_io *eio, bhnd_size_t offset, u_int width)
{
	struct bhnd_erom_iobus *iobus = (struct bhnd_erom_iobus *)eio;

	if (!iobus->mapped) 
		panic("no active mapping");

	if (iobus->limit < width || iobus->limit - width < offset)
		panic("invalid offset %#jx", offset);

	switch (width) {
	case 1:
		return (bus_space_read_1(iobus->bst, iobus->bsh,
		    iobus->offset + offset));
	case 2:
		return (bus_space_read_2(iobus->bst, iobus->bsh,
		    iobus->offset + offset));
	case 4:
		return (bus_space_read_4(iobus->bst, iobus->bsh,
		    iobus->offset + offset));
	default:
		panic("invalid width %u", width);
	}
}
