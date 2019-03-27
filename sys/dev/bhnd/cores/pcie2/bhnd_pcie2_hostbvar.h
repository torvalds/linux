/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
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

#ifndef _BHND_CORES_PCIE2_BHND_PCI_HOSTBVAR_H_
#define _BHND_CORES_PCIE2_BHND_PCI_HOSTBVAR_H_

/*
 * PCIe-Gen2 Host Bridge definitions.
 */

#include <sys/param.h>
#include <sys/bus.h>

#include "bhnd_pcie2_var.h"

DECLARE_CLASS(bhnd_pcie2_hostb_driver);


/* 
 * PCIe-Gen2 endpoint-mode device quirks
 */
enum {
	/**
	 * The PCIe SerDes output should be configured with an amplitude of
	 * 1214mVpp and a differential output de-emphasis of -8.46dB.
	 *
	 * The exact issue this workaround resolves is unknown.
	 */
	BHND_PCIE2_QUIRK_SERDES_TXDRV_DEEMPH	= (1<<0),
};


/**
 * bhnd_pci_hostb driver instance state.
 */
struct bhnd_pcie2hb_softc {
	struct bhnd_pcie2_softc	common;		/**< common bhnd_pcie2 state */
	device_t		dev;
	device_t		pci_dev;	/**< host PCI device */
	uint32_t		quirks;		/**< hostb device quirks */
};


#endif /* _BHND_CORES_PCIE2_BHND_PCI_HOSTBVAR_H_ */
