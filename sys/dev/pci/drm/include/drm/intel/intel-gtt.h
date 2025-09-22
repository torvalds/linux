/* Public domain. */

#ifndef _DRM_INTEL_GTT_H
#define _DRM_INTEL_GTT_H

#include <linux/agp_backend.h>
#include <linux/kernel.h>

struct pci_dev;
struct sg_table;

int intel_gmch_enable_gtt(void);
void intel_gmch_gtt_flush(void);
int intel_gmch_probe(struct pci_dev *, struct pci_dev *, void *);
void intel_gmch_gtt_get(u64 *, phys_addr_t *, resource_size_t *);
void intel_gmch_gtt_insert_sg_entries(struct sg_table *, unsigned int,
    unsigned int);
void intel_gmch_gtt_insert_page(dma_addr_t, unsigned int, unsigned int);
void intel_gmch_gtt_clear_range(unsigned int, unsigned int);
void intel_gmch_remove(void);

#endif
