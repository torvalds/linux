/* Common header for intel-gtt.ko and i915.ko */

#ifndef _DRM_INTEL_GTT_H
#define	_DRM_INTEL_GTT_H

struct intel_gtt {
	/* Size of memory reserved for graphics by the BIOS */
	unsigned int stolen_size;
	/* Total number of gtt entries. */
	unsigned int gtt_total_entries;
	/* Part of the gtt that is mappable by the cpu, for those chips where
	 * this is not the full gtt. */
	unsigned int gtt_mappable_entries;
	/* needed for ioremap in drm/i915 */
	phys_addr_t gma_bus_addr;
} *intel_gtt_get(void);

int intel_gmch_probe(struct pci_dev *bridge_pdev, struct pci_dev *gpu_pdev,
		     struct agp_bridge_data *bridge);
void intel_gmch_remove(void);

bool intel_enable_gtt(void);

void intel_gtt_chipset_flush(void);
void intel_gtt_insert_sg_entries(struct sg_table *st,
				 unsigned int pg_start,
				 unsigned int flags);
void intel_gtt_clear_range(unsigned int first_entry, unsigned int num_entries);

/* Special gtt memory types */
#define AGP_DCACHE_MEMORY	1
#define AGP_PHYS_MEMORY		2

/* flag for GFDT type */
#define AGP_USER_CACHED_MEMORY_GFDT (1 << 3)

#ifdef CONFIG_INTEL_IOMMU
extern int intel_iommu_gfx_mapped;
#endif

#endif
