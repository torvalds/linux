/* Common header for intel-gtt.ko and i915.ko */

#ifndef _DRM_INTEL_GTT_H
#define	_DRM_INTEL_GTT_H

const struct intel_gtt {
	/* Size of memory reserved for graphics by the BIOS */
	unsigned int stolen_size;
	/* Total number of gtt entries. */
	unsigned int gtt_total_entries;
	/* Part of the gtt that is mappable by the cpu, for those chips where
	 * this is not the full gtt. */
	unsigned int gtt_mappable_entries;
} *intel_gtt_get(void);

#endif

