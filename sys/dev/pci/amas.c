/*	$OpenBSD: amas.c,v 1.7 2022/03/11 18:00:45 mpi Exp $	*/

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
 */

#include <dev/pci/amas.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

int amas_match(struct device*, void*, void*);
void amas_attach(struct device*, struct device*, void*);

/*
 * Amas device layout:
 *
 * - base/limit registers (on 0x0f, 0x10, 0x11)
 * - extended base/limit registers (on 0x10)
 *
 * 0x0f, 0x10 support up to 8 nodes
 * 0x11 supports up to 1 nodes
 *
 * base/limit registers use bits [31..16] to indicate address [39..24]
 * extended base/limit registers use bits [7..0] to indicate address [47..40]
 * base/limit addresses need to be shifted <<24 for memory address
 * extended base/limit addresses need to be shifted <<40 for memory address
 */

#define AMAS_REG_BASE(node)	(0x0040 + 0x08 * (node))
#define AMAS_REG_LIMIT(node)	(0x0044 + 0x08 * (node))
#define AMAS_REG_EXTBASE(node)	(0x0140 + 0x08 * (node))
#define AMAS_REG_EXTLIMIT(node)	(0x0144 + 0x08 * (node))

#define AMAS_REG_BL_ADDR(reg)	(((reg) >> 16) & 0xffff)
#define AMAS_REG_EBL_ADDR(ereg)	((ereg) & 0xff)

#define AMAS_REG_BL_SHIFT	(24)
#define AMAS_REG_EBL_SHIFT	(40)

#define AMAS_REG_BL_PGSHIFT	(AMAS_REG_BL_SHIFT - PAGE_SHIFT)
#define AMAS_REG_EBL_PGSHIFT	(AMAS_REG_EBL_SHIFT - PAGE_SHIFT)

/*
 * Convert an address in amas to a page number.
 *
 * The device uses an inclusive mapping, where the upper bound address
 * must be all 1's after shifting.
 * The device driver uses C-style array indices, hence the +1 in the _LIMIT
 * macro.
 */
#define AMAS_ADDR2PAGE_BASE(base, ebase)				\
    (((base) << AMAS_REG_BL_PGSHIFT) | ((ebase) << AMAS_REG_EBL_PGSHIFT))
#define AMAS_ADDR2PAGE_LIMIT(base, ebase)				\
    (((base + 1) << AMAS_REG_BL_PGSHIFT) | ((ebase) << AMAS_REG_EBL_PGSHIFT))

/*
 * Node and interleave description.
 * - base contains node selection [10..8] (on 0x0f, 0x10)
 * - limit contains node selection bitmask [10..8] (on 0x0f, 0x10)
 * - limit contains destination node [2..0] (on 0x0f, 0x10)
 */
#define AMAS_DST_NODE(base, limit)	((limit) & 0x07)
#define AMAS_INTL_ENABLE(base, limit)	(((base) >> 8) & 0x07)
#define AMAS_INTL_SELECTOR(base, limit)	(((limit) >> 8) & 0x07)

/*
 * Defines for family.
 * Corresponds to the amas_feature[] constant below.
 */
#define AMAS_FAM_0Fh		(0)
#define AMAS_FAM_10h		(1)
#define AMAS_FAM_11h		(2)

/*
 * Feature tests.
 *
 * 0x11 supports at max 1 node, 0x0f and 0x10 support up to 8 nodes.
 * 0x11 has extended address registers.
 * 0x0f, 0x10 can interleave memory.
 */
struct amas_feature_t {
	int maxnodes;
	int can_intl;
	int has_extended_bl;
};
static const struct amas_feature_t amas_feature[] = {
	/* Family 0x0f */
	{ 8, 1, 0 },
	/* Family 0x10 */
	{ 8, 1, 1 },
	/* Family 0x11 */
	{ 1, 0, 0 },
};

/* Probe code. */
const struct cfattach amas_ca = {
	sizeof(struct amas_softc),
	amas_match,
	amas_attach
};

struct cfdriver amas_cd = {
	NULL,
	"amas",
	DV_DULL
};

const struct pci_matchid amas_devices[] = {
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_0F_ADDR },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_10_ADDR },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_11_ADDR },
};

int
amas_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args* pa = aux;

	if (pci_matchbyid(pa, amas_devices, nitems(amas_devices)))
		return 2; /* override pchb */
	return 0;
}

void
amas_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct amas_softc *amas = (struct amas_softc*)self;
#ifdef DEBUG
	paddr_t start_pg, end_pg;
	int nodes, i;
#endif /* DEBUG */

	amas->pa_tag = pa->pa_tag;
	amas->pa_pc = pa->pa_pc;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_AMD_0F_ADDR:
		amas->family = AMAS_FAM_0Fh;
		break;
	case PCI_PRODUCT_AMD_10_ADDR:
		amas->family = AMAS_FAM_10h;
		break;
	case PCI_PRODUCT_AMD_11_ADDR:
		amas->family = AMAS_FAM_11h;
		break;
	}

#ifdef DEBUG
	nodes = amas_intl_nodes(amas);

	printf(":");
	if (nodes != 0) {
		printf(" interleaved");
	} else {
		for (i = 0; i < AMAS_MAX_NODES; i++) {
			amas_get_pagerange(amas, i, &start_pg, &end_pg);

			if (!(start_pg == 0 && end_pg == 0))
				printf(" [%#lx, %#lx]", start_pg, end_pg);
		}
	}
#endif /* DEBUG */
	printf("\n");

	return;
}

/*
 * Returns the number of nodes across which the memory is interleaved.
 * Returns 0 if the memory is not interleaved.
 */
int
amas_intl_nodes(struct amas_softc *amas)
{
	pcireg_t base_reg, limit_reg;
	int mask;

	if (!amas_feature[amas->family].can_intl)
		return 0;

	/*
	 * Use node 0 on amas device to find interleave information.
	 * Node 0 is always present.
	 */

	base_reg = pci_conf_read(amas->pa_pc, amas->pa_tag, AMAS_REG_BASE(0));
	limit_reg = pci_conf_read(amas->pa_pc, amas->pa_tag, AMAS_REG_LIMIT(0));
	mask = AMAS_INTL_ENABLE(base_reg, limit_reg);

	return mask == 0 ? 0 : mask + 1;
}

/*
 * Returns the range of memory that is contained on the given node.
 * If the memory is interleaved, the result is undefined.
 *
 * The range is written in {start,end}_pg_idx.
 * Note that these are page numbers and that these use array indices:
 * pages are in this range if start <= pg_no < end.
 *
 * This device supports at most 8 nodes.
 */
void
amas_get_pagerange(struct amas_softc *amas, int node,
    paddr_t *start_pg_idx, paddr_t *end_pg_idx)
{
	pcireg_t base, ebase, limit, elimit;
	paddr_t base_addr, ebase_addr, limit_addr, elimit_addr;

	/* Sanity check: max AMAS_MAX_NODES supported. */
	KASSERT(node >= 0 && node < AMAS_MAX_NODES);

	if (node >= amas_feature[amas->family].maxnodes) {
		/* Unsupported node: bail out early. */
		*start_pg_idx = 0;
		*end_pg_idx = 0;
		return;
	}

	base = pci_conf_read(amas->pa_pc, amas->pa_tag,
	    AMAS_REG_BASE(node));
	limit = pci_conf_read(amas->pa_pc, amas->pa_tag,
	    AMAS_REG_LIMIT(node));
	base_addr = AMAS_REG_BL_ADDR(base);
	limit_addr = AMAS_REG_BL_ADDR(limit);

	ebase = 0;
	elimit = 0;
	ebase_addr = 0;
	elimit_addr = 0;
#if 0 /* Needs extended pci registers. */
	if (amas_feature[amas->family].has_extended_bl) {
		ebase = pci_conf_read(amas->pa_pc, amas->pa_tag,
		    AMAS_REG_EXTBASE(node));
		elimit = pci_conf_read(amas->pa_pc, amas->pa_tag,
		    AMAS_REG_EXTLIMIT(node));
		ebase_addr = AMAS_REG_EBL_ADDR(ebase);
		elimit_addr = AMAS_REG_EBL_ADDR(elimit);
	}
#endif /* 0 */

	if (ebase_addr > elimit_addr ||
	    (ebase_addr == elimit_addr && base_addr >= limit_addr)) {
		/* no memory present */
		*start_pg_idx = 0;
		*end_pg_idx = 0;
		return;
	}

	/* Guaranteed by spec. */
	KASSERT(node == AMAS_DST_NODE(base, limit));

	*start_pg_idx = AMAS_ADDR2PAGE_BASE(base_addr, ebase_addr);
	*end_pg_idx = AMAS_ADDR2PAGE_LIMIT(limit_addr, elimit_addr);
	return;
}
