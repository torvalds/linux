/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
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

#ifndef _BHND_CORES_PCIE2_BHND_PCIE2_VAR_H_
#define _BHND_CORES_PCIE2_BHND_PCIE2_VAR_H_

#include <sys/param.h>
#include <sys/bus.h>

/*
 * Shared PCIe-G2 Bridge/Host Bridge definitions.
 */

DECLARE_CLASS(bhnd_pcie2_driver);
struct bhnd_pcie2_softc;

int		bhnd_pcie2_generic_probe(device_t dev);
int		bhnd_pcie2_generic_attach(device_t dev);
int		bhnd_pcie2_generic_detach(device_t dev);
int		bhnd_pcie2_generic_suspend(device_t dev);
int		bhnd_pcie2_generic_resume(device_t dev);


uint32_t	bhnd_pcie2_read_proto_reg(struct bhnd_pcie2_softc *sc,
		    uint32_t addr);
void		bhnd_pcie2_write_proto_reg(struct bhnd_pcie2_softc *sc,
		    uint32_t addr, uint32_t val);
int		bhnd_pcie2_mdio_read(struct bhnd_pcie2_softc *sc, int phy,
		    int reg);
int		bhnd_pcie2_mdio_write(struct bhnd_pcie2_softc *sc, int phy,
		    int reg, int val);
int		bhnd_pcie2_mdio_read_ext(struct bhnd_pcie2_softc *sc, int phy,
		    int devaddr, int reg);
int		bhnd_pcie2_mdio_write_ext(struct bhnd_pcie2_softc *sc,
		    int phy, int devaddr, int reg, int val);

/**
 * bhnd_pcie2 child device info
 */
struct bhnd_pcie2_devinfo {
	struct resource_list	resources;
};

/*
 * Generic PCIe-G2 bridge/end-point driver state.
 * 
 * Must be first member of all subclass softc structures.
 */
struct bhnd_pcie2_softc {
	device_t		 dev;		/**< pci device */
	uint32_t		 quirks;	/**< quirk flags */

	struct mtx		 mtx;		/**< state mutex used to protect
						     interdependent register
						     accesses. */

	struct bhnd_resource	*mem_res;	/**< device register block. */
	int			 mem_rid;	/**< register block RID */
};


#define	BHND_PCIE2_LOCK_INIT(sc) \
	mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev), \
	    "BHND PCIe-G2 driver lock", MTX_DEF)
#define	BHND_PCIE2_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	BHND_PCIE2_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	BHND_PCIE2_LOCK_ASSERT(sc, what)	mtx_assert(&(sc)->mtx, what)
#define	BHND_PCIE2_LOCK_DESTROY(sc)		mtx_destroy(&(sc)->mtx)

#endif /* _BHND_CORES_PCIE2_BHND_PCIE2_VAR_H_ */
