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

#ifndef	_MIPS_BROADCOM_BCM_NVRAM_CFE_H_
#define	_MIPS_BROADCOM_BCM_NVRAM_CFE_H_

#include <sys/param.h>
#include <sys/bus.h>

#include <dev/bhnd/nvram/bhnd_nvram.h>
#include <dev/bhnd/nvram/bhnd_nvram_iovar.h>
#include <dev/bhnd/nvram/bhnd_nvram_store.h>

struct bcm_nvram_iocfe;

int		bcm_nvram_find_cfedev(struct bcm_nvram_iocfe *iocfe,
		    bhnd_nvram_data_class **cls);

/**
 * CFE-backed bhnd_nvram_io implementation.
 */
struct bcm_nvram_iocfe {
	struct bhnd_nvram_io	 io;		/**< common I/O instance state */

	char			*dname;		/**< CFE device name (borrowed) */
	int			 fd;		/**< CFE file descriptor */
	size_t			 offset;	/**< base offset */
	size_t			 size;		/**< device size */
	bool			 req_blk_erase;	/**< flash blocks must be erased
						     before writing */
};

/** bhnd_nvram_cfe driver instance state. */
struct bhnd_nvram_cfe_softc {
	device_t		 	 dev;
	struct bhnd_nvram_store		*store;	/**< nvram store */
};

#endif /* _MIPS_BROADCOM_BCM_NVRAM_CFE_H_ */
