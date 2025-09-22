/*	$OpenBSD: amas.h,v 1.1 2009/05/07 11:30:27 ariane Exp $	*/

/*
 * Copyright (c) 2009 Ariane van der Steldt <ariane@stack.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Device: amas (AMD memory access/address switch).
 *
 * Driver for the amd athlon/opteron 64 address map.
 * This device is integrated in 64-bit Athlon and Opteron cpus
 * and contains mappings for memory to processor nodes.
 *
 * The AMD cpu can access memory in two ways: interleaved and non-interleaved.
 *
 * In interleaved mode, 16 MB regions are rotated across each node,
 * example for 2 nodes:
 * - region 0 (0-16 MB) on node 0.
 * - region 1 (16-32 MB) on node 1.
 * - region 2 (33-48 MB) on node 0.
 * - region 3 (48-64 MB) on node 1.
 * ...
 * - region 2 * N on node 0.
 * - region 2 * N + 1 on node 1.
 * Interleaved mode requires that each node has the same amount of physical
 * memory.
 *
 * In non-interleaved mode, memory for each node is a continuous address space,
 * example for 2 nodes:
 * - region 0 (all memory on CPU package 0) on node 0.
 * - region 1 (all memory on CPU package 1) on node 1.
 * Non-interleaved mode requires either that each node has the same amount of
 * physical memory or that memory holes are allowed between each node.
 *
 * Configuring memory for interleaved or non-interleaved mode is handled by
 * the BIOS.
 */

#ifndef _MACHINE_AMAS_H_
#define _MACHINE_AMAS_H_

#include <sys/types.h>
#include <sys/device.h>

#include <dev/pci/pcivar.h>

#ifdef _KERNEL

#define AMAS_MAX_NODES (8) /* AMAS supports 8 nodes at maximum. */

/* Device configuration. */
struct amas_softc {
	struct device sc_dev;
	/* PCI location of this device. */
	pcitag_t pa_tag;
	pci_chipset_tag_t pa_pc;
	int family;
};

/* AMAS driver. */
extern struct cfdriver amas_cd;

int amas_match(struct device*, void*, void*);
void amas_attach(struct device*, struct device*, void*);

int amas_intl_nodes(struct amas_softc*);
void amas_get_pagerange(struct amas_softc*, int node, paddr_t*, paddr_t*);

#endif /* _KERNEL */
#endif /*  _MACHINE_AMAS_H_ */
