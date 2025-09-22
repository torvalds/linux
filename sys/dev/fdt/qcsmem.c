/*	$OpenBSD: qcsmem.c,v 1.1 2023/05/19 21:13:49 patrick Exp $	*/
/*
 * Copyright (c) 2023 Patrick Wildt <patrick@blueri.se>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/atomic.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define QCSMEM_ITEM_FIXED	8
#define QCSMEM_ITEM_COUNT	512
#define QCSMEM_HOST_COUNT	15

struct qcsmem_proc_comm {
	uint32_t command;
	uint32_t status;
	uint32_t params[2];
};

struct qcsmem_global_entry {
	uint32_t allocated;
	uint32_t offset;
	uint32_t size;
	uint32_t aux_base;
#define QCSMEM_GLOBAL_ENTRY_AUX_BASE_MASK	0xfffffffc
};

struct qcsmem_header {
	struct qcsmem_proc_comm proc_comm[4];
	uint32_t version[32];
#define QCSMEM_HEADER_VERSION_MASTER_SBL_IDX	7
#define QCSMEM_HEADER_VERSION_GLOBAL_HEAP	11
#define QCSMEM_HEADER_VERSION_GLOBAL_PART	12
	uint32_t initialized;
	uint32_t free_offset;
	uint32_t available;
	uint32_t reserved;
	struct qcsmem_global_entry toc[QCSMEM_ITEM_COUNT];
};

struct qcsmem_ptable_entry {
	uint32_t offset;
	uint32_t size;
	uint32_t flags;
	uint16_t host[2];
#define QCSMEM_LOCAL_HOST			0
#define QCSMEM_GLOBAL_HOST			0xfffe
	uint32_t cacheline;
	uint32_t reserved[7];
};

struct qcsmem_ptable {
	uint32_t magic;
#define QCSMEM_PTABLE_MAGIC	0x434f5424
	uint32_t version;
#define QCSMEM_PTABLE_VERSION	1
	uint32_t num_entries;
	uint32_t reserved[5];
	struct qcsmem_ptable_entry entry[];
};

struct qcsmem_partition_header {
	uint32_t magic;
#define QCSMEM_PART_HDR_MAGIC	0x54525024
	uint16_t host[2];
	uint32_t size;
	uint32_t offset_free_uncached;
	uint32_t offset_free_cached;
	uint32_t reserved[3];
};

struct qcsmem_partition {
	struct qcsmem_partition_header *phdr;
	size_t cacheline;
	size_t size;
};

struct qcsmem_private_entry {
	uint16_t canary;
#define QCSMEM_PRIV_ENTRY_CANARY	0xa5a5
	uint16_t item;
	uint32_t size;
	uint16_t padding_data;
	uint16_t padding_hdr;
	uint32_t reserved;
};

struct qcsmem_info {
	uint32_t magic;
#define QCSMEM_INFO_MAGIC	0x49494953
	uint32_t size;
	uint32_t base_addr;
	uint32_t reserved;
	uint32_t num_items;
};

struct qcsmem_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	bus_addr_t		sc_aux_base;
	bus_size_t		sc_aux_size;

	int			sc_item_count;
	struct qcsmem_partition	sc_global_partition;
	struct qcsmem_partition	sc_partitions[QCSMEM_HOST_COUNT];
};

struct qcsmem_softc *qcsmem_sc;

int	qcsmem_match(struct device *, void *, void *);
void	qcsmem_attach(struct device *, struct device *, void *);

const struct cfattach qcsmem_ca = {
	sizeof (struct qcsmem_softc), qcsmem_match, qcsmem_attach
};

struct cfdriver qcsmem_cd = {
	NULL, "qcsmem", DV_DULL
};

int
qcsmem_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,smem");
}

void
qcsmem_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcsmem_softc *sc = (struct qcsmem_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct qcsmem_header *header;
	struct qcsmem_ptable *ptable;
	struct qcsmem_ptable_entry *pte;
	struct qcsmem_info *info;
	struct qcsmem_partition *part;
	struct qcsmem_partition_header *phdr;
	uint32_t version;
	int i;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}
	sc->sc_aux_base = faa->fa_reg[0].addr;
	sc->sc_aux_size = faa->fa_reg[0].addr;

	ptable = bus_space_vaddr(sc->sc_iot, sc->sc_ioh) +
	    faa->fa_reg[0].size - PAGE_SIZE;
	if (ptable->magic != QCSMEM_PTABLE_MAGIC ||
	    ptable->version != QCSMEM_PTABLE_VERSION) {
		printf(": unsupported ptable 0x%x/0x%x\n",
		    ptable->magic, ptable->version);
		bus_space_unmap(sc->sc_iot, sc->sc_ioh,
		    faa->fa_reg[0].size);
		return;
	}

	header = bus_space_vaddr(sc->sc_iot, sc->sc_ioh);
	version = header->version[QCSMEM_HEADER_VERSION_MASTER_SBL_IDX] >> 16;
	if (version != QCSMEM_HEADER_VERSION_GLOBAL_PART) {
		printf(": unsupported header 0x%x\n", version);
		return;
	}

	for (i = 0; i < ptable->num_entries; i++) {
		pte = &ptable->entry[i];
		if (!pte->offset || !pte->size)
			continue;
		if (pte->host[0] == QCSMEM_GLOBAL_HOST &&
		    pte->host[1] == QCSMEM_GLOBAL_HOST)
			part = &sc->sc_global_partition;
		else if (pte->host[0] == QCSMEM_LOCAL_HOST &&
		    pte->host[1] < QCSMEM_HOST_COUNT)
			part = &sc->sc_partitions[pte->host[1]];
		else if (pte->host[1] == QCSMEM_LOCAL_HOST &&
		    pte->host[0] < QCSMEM_HOST_COUNT)
			part = &sc->sc_partitions[pte->host[0]];
		else
			continue;
		if (part->phdr != NULL)
			continue;
		phdr = bus_space_vaddr(sc->sc_iot, sc->sc_ioh) +
		    pte->offset;
		if (phdr->magic != QCSMEM_PART_HDR_MAGIC) {
			printf(": unsupported partition 0x%x\n",
			    phdr->magic);
			return;
		}
		if (pte->host[0] != phdr->host[0] ||
		    pte->host[1] != phdr->host[1]) {
			printf(": bad hosts 0x%x/0x%x+0x%x/0x%x\n",
			    pte->host[0], phdr->host[0],
			    pte->host[1], phdr->host[1]);
			return;
		}
		if (pte->size != phdr->size) {
			printf(": bad size 0x%x/0x%x\n",
			    pte->size, phdr->size);
			return;
		}
		if (phdr->offset_free_uncached > phdr->size) {
			printf(": bad size 0x%x > 0x%x\n",
			    phdr->offset_free_uncached, phdr->size);
			return;
		}
		part->phdr = phdr;
		part->size = pte->size;
		part->cacheline = pte->cacheline;
	}
	if (sc->sc_global_partition.phdr == NULL) {
		printf(": could not find global partition\n");
		return;
	}

	sc->sc_item_count = QCSMEM_ITEM_COUNT;
	info = (struct qcsmem_info *)&ptable->entry[ptable->num_entries];
	if (info->magic == QCSMEM_INFO_MAGIC)
		sc->sc_item_count = info->num_items;

	printf("\n");

	qcsmem_sc = sc;
}

int
qcsmem_alloc_private(struct qcsmem_softc *sc, struct qcsmem_partition *part,
    int item, int size)
{
	struct qcsmem_private_entry *entry, *last;
	struct qcsmem_partition_header *phdr = part->phdr;

	entry = (void *)&phdr[1];
	last = (void *)phdr + phdr->offset_free_uncached;

	if ((void *)last > (void *)phdr + part->size)
		return EINVAL;

	while (entry < last) {
		if (entry->canary != QCSMEM_PRIV_ENTRY_CANARY) {
			printf("%s: invalid canary\n", sc->sc_dev.dv_xname);
			return EINVAL;
		}

		if (entry->item == item)
			return 0;

		entry = (void *)&entry[1] + entry->padding_hdr +
		    entry->size;
	}

	if ((void *)entry > (void *)phdr + part->size)
		return EINVAL;

	if ((void *)&entry[1] + roundup(size, 8) >
	    (void *)phdr + phdr->offset_free_cached)
		return EINVAL;

	entry->canary = QCSMEM_PRIV_ENTRY_CANARY;
	entry->item = item;
	entry->size = roundup(size, 8);
	entry->padding_data = entry->size - size;
	entry->padding_hdr = 0;
	membar_producer();

	phdr->offset_free_uncached += sizeof(*entry) + entry->size;

	return 0;
}

int
qcsmem_alloc_global(struct qcsmem_softc *sc, int item, int size)
{
	struct qcsmem_header *header;
	struct qcsmem_global_entry *entry;

	header = bus_space_vaddr(sc->sc_iot, sc->sc_ioh);
	entry = &header->toc[item];
	if (entry->allocated)
		return 0;

	size = roundup(size, 8);
	if (size > header->available)
		return EINVAL;

	entry->offset = header->free_offset;
	entry->size = size;
	membar_producer();
	entry->allocated = 1;

	header->free_offset += size;
	header->available -= size;

	return 0;
}

int
qcsmem_alloc(int host, int item, int size)
{
	struct qcsmem_softc *sc = qcsmem_sc;
	struct qcsmem_partition *part;
	int ret;

	if (sc == NULL)
		return ENXIO;

	if (item < QCSMEM_ITEM_FIXED)
		return EPERM;

	if (item >= sc->sc_item_count)
		return ENXIO;

	ret = hwlock_lock_idx_timeout(sc->sc_node, 0, 1000);
	if (ret)
		return ret;

	if (host < QCSMEM_HOST_COUNT &&
	    sc->sc_partitions[host].phdr != NULL) {
		part = &sc->sc_partitions[host];
		ret = qcsmem_alloc_private(sc, part, item, size);
	} else if (sc->sc_global_partition.phdr != NULL) {
		part = &sc->sc_global_partition;
		ret = qcsmem_alloc_private(sc, part, item, size);
	} else {
		ret = qcsmem_alloc_global(sc, item, size);
	}

	hwlock_unlock_idx(sc->sc_node, 0);
	return ret;
}

void *
qcsmem_get_private(struct qcsmem_softc *sc, struct qcsmem_partition *part,
    int item, int *size)
{
	struct qcsmem_private_entry *entry, *last;
	struct qcsmem_partition_header *phdr = part->phdr;

	entry = (void *)&phdr[1];
	last = (void *)phdr + phdr->offset_free_uncached;

	while (entry < last) {
		if (entry->canary != QCSMEM_PRIV_ENTRY_CANARY) {
			printf("%s: invalid canary\n", sc->sc_dev.dv_xname);
			return NULL;
		}

		if (entry->item == item) {
			if (size != NULL) {
				if (entry->size > part->size ||
				    entry->padding_data > entry->size)
					return NULL;
				*size = entry->size - entry->padding_data;
			}

			return (void *)&entry[1] + entry->padding_hdr;
		}

		entry = (void *)&entry[1] + entry->padding_hdr +
		    entry->size;
	}

	if ((void *)entry > (void *)phdr + part->size)
		return NULL;

	entry = (void *)phdr + phdr->size -
	    roundup(sizeof(*entry), part->cacheline);
	last = (void *)phdr + phdr->offset_free_cached;

	if ((void *)entry < (void *)phdr ||
	    (void *)last > (void *)phdr + part->size)
		return NULL;

	while (entry > last) {
		if (entry->canary != QCSMEM_PRIV_ENTRY_CANARY) {
			printf("%s: invalid canary\n", sc->sc_dev.dv_xname);
			return NULL;
		}

		if (entry->item == item) {
			if (size != NULL) {
				if (entry->size > part->size ||
				    entry->padding_data > entry->size)
					return NULL;
				*size = entry->size - entry->padding_data;
			}

			return (void *)entry - entry->size;
		}

		entry = (void *)entry - entry->size -
		    roundup(sizeof(*entry), part->cacheline);
	}

	if ((void *)entry < (void *)phdr)
		return NULL;

	return NULL;
}

void *
qcsmem_get_global(struct qcsmem_softc *sc, int item, int *size)
{
	struct qcsmem_header *header;
	struct qcsmem_global_entry *entry;
	uint32_t aux_base;

	header = bus_space_vaddr(sc->sc_iot, sc->sc_ioh);
	entry = &header->toc[item];
	if (!entry->allocated)
		return NULL;

	aux_base = entry->aux_base & QCSMEM_GLOBAL_ENTRY_AUX_BASE_MASK;
	if (aux_base != 0 && aux_base != sc->sc_aux_base)
		return NULL;

	if (entry->size + entry->offset > sc->sc_aux_size)
		return NULL;

	if (size != NULL)
		*size = entry->size;

	return bus_space_vaddr(sc->sc_iot, sc->sc_ioh) + entry->offset;
}

void *
qcsmem_get(int host, int item, int *size)
{
	struct qcsmem_softc *sc = qcsmem_sc;
	struct qcsmem_partition *part;
	void *p = NULL;
	int ret;

	if (sc == NULL)
		return NULL;

	if (item >= sc->sc_item_count)
		return NULL;

	ret = hwlock_lock_idx_timeout(sc->sc_node, 0, 1000);
	if (ret)
		return NULL;

	if (host < QCSMEM_HOST_COUNT &&
	    sc->sc_partitions[host].phdr != NULL) {
		part = &sc->sc_partitions[host];
		p = qcsmem_get_private(sc, part, item, size);
	} else if (sc->sc_global_partition.phdr != NULL) {
		part = &sc->sc_global_partition;
		p = qcsmem_get_private(sc, part, item, size);
	} else {
		p = qcsmem_get_global(sc, item, size);
	}

	hwlock_unlock_idx(sc->sc_node, 0);
	return p;
}
