/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Authors: Joe Kloss; Ravi Pokala (rpokala@freebsd.org)
 *
 * Copyright (c) 2017-2018 Panasas
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
 *
 * $FreeBSD$
 */

#ifndef _DEV__IMCSMB__IMCSMB_VAR_H_
#define _DEV__IMCSMB__IMCSMB_VAR_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/atomic.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/smbus/smbconf.h>

#include "smbus_if.h"

/* A detailed description of this device is present in imcsmb_pci.c */

/**
 * The softc for a particular instance of the PCI device associated with a pair
 * of iMC-SMB controllers.
 *
 * Ordinarily, locking would be done with a mutex. However, we might have an
 * NVDIMM connected to this SMBus, and we might need to issue the SAVE command
 * to the NVDIMM from a panic context. Mutex operations are not allowed while
 * the scheduler is stopped, so just use a simple semaphore.
 *
 * If, as described in the manpage, additional steps are needed to stop/restart
 * firmware operations before/after using the controller, then additional fields
 * can be added to this softc.
 */
struct imcsmb_pci_softc {
	device_t dev;
	volatile int semaphore;
};

void imcsmb_pci_release_bus(device_t dev);
int imcsmb_pci_request_bus(device_t dev);

/**
 * PCI config registers for each individual SMBus controller within the iMC.
 * Each iMC-SMB has a separate set of registers. There is an array of these
 * structures for the PCI device, and one of them is passed to driver for the
 * actual iMC-SMB as the IVAR.
 */
struct imcsmb_reg_set {
	uint16_t smb_stat;
	uint16_t smb_cmd;
	uint16_t smb_cntl;
};

/**
 * The softc for the device associated with a particular iMC-SMB controller.
 * There are two such controllers for each of the PCI devices. The PCI driver
 * tells the iMC-SMB driver which set of registers to use via the IVAR. This
 * technique was suggested by John Baldwin (jhb@).
 */
struct imcsmb_softc {
	device_t dev;
	device_t imcsmb_pci;	/* The SMBus controller's parent iMC */
	device_t smbus;		/* The child smbusX interface */
	struct imcsmb_reg_set *regs;	/* The registers this controller uses */
};

#endif /* _DEV__IMCSMB__IMCSMB_VAR_H_ */

/* vi: set ts=8 sw=4 sts=8 noet: */
