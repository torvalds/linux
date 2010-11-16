/* Common header for intel-gtt.ko and i915.ko */

#ifndef _DRM_INTEL_GTT_H
#define	_DRM_INTEL_GTT_H
struct intel_gtt {
	/* Number of stolen gtt entries at the beginning. */
	unsigned int gtt_stolen_entries;
	/* Total number of gtt entries. */
	unsigned int gtt_total_entries;
	/* Part of the gtt that is mappable by the cpu, for those chips where
	 * this is not the full gtt. */
	unsigned int gtt_mappable_entries;
};

struct intel_gtt *intel_gtt_get(void);

#endif

