/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
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
 * 
 * $FreeBSD$
 */

#ifndef _BHND_BHNDB_PCIVAR_H_
#define _BHND_BHNDB_PCIVAR_H_

#include "bhndbvar.h"

/*
 * bhndb(4) PCI driver subclass.
 */

DECLARE_CLASS(bhndb_pci_driver);

struct bhndb_pci_softc;

/*
 * An interconnect-specific function implementing BHNDB_SET_WINDOW_ADDR
 */
typedef int (*bhndb_pci_set_regwin_t)(device_t dev, device_t pci_dev,
	         const struct bhndb_regwin *rw, bhnd_addr_t addr);

/**
 * PCI/PCIe bridge-level device quirks
 */
enum {
	/** No quirks */
	BHNDB_PCI_QUIRK_NONE		= 0,

	/**
	 * The core requires fixup of the BAR0 SROM shadow to point at the
	 * current PCI core.
	 */
	BHNDB_PCI_QUIRK_SRSH_WAR	= (1<<0),

	/**
	 * The PCI (rev <= 5) core does not provide interrupt status/mask
	 * registers; these siba-only devices require routing backplane
	 * interrupt flags via the SIBA_CFG0_INTVEC register.
	 */
	BHNDB_PCI_QUIRK_SIBA_INTVEC	= (1<<1),
};

/** bhndb_pci quirk table entry */
struct bhndb_pci_quirk {
	struct bhnd_chip_match	chip_desc;	/**< chip match descriptor */
	struct bhnd_core_match	core_desc;	/**< core match descriptor */
	uint32_t		quirks;		/**< quirk flags */
};

#define	BHNDB_PCI_QUIRK(_rev, _flags)	{			\
	{ BHND_MATCH_ANY },						\
	{ BHND_MATCH_CORE_REV(_rev) },					\
	_flags,								\
}

#define	BHNDB_PCI_QUIRK_END	\
	{ { BHND_MATCH_ANY },  { BHND_MATCH_ANY }, 0 }

#define	BHNDB_PCI_IS_QUIRK_END(_q)	\
	(BHND_MATCH_IS_ANY(&(_q)->core_desc) &&	\
	 BHND_MATCH_IS_ANY(&(_q)->chip_desc) &&	\
	 (_q)->quirks == 0)

/** bhndb_pci core table entry */
struct bhndb_pci_core {
	struct bhnd_core_match	 match;		/**< core match descriptor */
	struct bhndb_pci_quirk	*quirks;	/**< quirk table */
};

#define	BHNDB_PCI_CORE(_device, _quirks) {				\
	{ BHND_MATCH_CORE(BHND_MFGID_BCM, BHND_COREID_ ## _device) },	\
	_quirks								\
}
#define	BHNDB_PCI_CORE_END		{ { BHND_MATCH_ANY }, NULL }
#define	BHNDB_PCI_IS_CORE_END(_c)	BHND_MATCH_IS_ANY(&(_c)->match)

struct bhndb_pci_softc {
	struct bhndb_softc	 bhndb;		/**< parent softc */
	device_t		 dev;		/**< bridge device */
	device_t		 parent;	/**< parent PCI device */
	bhnd_devclass_t		 pci_devclass;	/**< PCI core's devclass */
	uint32_t		 pci_quirks;	/**< PCI bridge-level quirks */
	int			 msi_count;	/**< MSI count, or 0 */
	struct bhndb_intr_isrc	*isrc;		/**< host interrupt source */

	struct mtx		 mtx;
	bhndb_pci_set_regwin_t	 set_regwin;	/**< regwin handler */
};

#define	BHNDB_PCI_LOCK_INIT(sc) \
	mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev), \
	    "bhndb_pc state", MTX_DEF)
#define	BHNDB_PCI_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	BHNDB_PCI_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	BHNDB_PCI_LOCK_ASSERT(sc, what)		mtx_assert(&(sc)->mtx, what)
#define	BHNDB_PCI_LOCK_DESTROY(sc)		mtx_destroy(&(sc)->mtx)

#endif /* _BHND_BHNDB_PCIVAR_H_ */
