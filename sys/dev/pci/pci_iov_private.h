/*-
 * Copyright (c) 2013-2015 Sandvine Inc.
 * All rights reserved.
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

#ifndef _PCI_IOV_PRIVATE_H_
#define _PCI_IOV_PRIVATE_H_

struct pci_iov_bar {
	struct resource *res;

	pci_addr_t bar_size;
	pci_addr_t bar_shift;
};

struct pcicfg_iov {
	struct cdev *iov_cdev;
	nvlist_t *iov_schema;

	struct pci_iov_bar iov_bar[PCIR_MAX_BAR_0 + 1];
	struct rman rman;
	char rman_name[64];
 
	int iov_pos;
	int iov_num_vfs;
	uint32_t iov_flags;

	uint16_t iov_ctl;
	uint32_t iov_page_size;
};

#define	IOV_RMAN_INITED		0x0001
#define	IOV_BUSY		0x0002

void	pci_iov_cfg_restore(device_t dev, struct pci_devinfo *dinfo);
void	pci_iov_cfg_save(device_t dev, struct pci_devinfo *dinfo);

#endif

