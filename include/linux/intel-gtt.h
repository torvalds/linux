/*
 * Common Intel AGPGART and GTT definitions.
 */
#ifndef _INTEL_GTT_H
#define _INTEL_GTT_H

#include <linux/agp_backend.h>

/* This is for Intel only GTT controls.
 *
 * Sandybridge: AGP_USER_CACHED_MEMORY default to LLC only
 */

#define AGP_USER_CACHED_MEMORY_LLC_MLC (AGP_USER_TYPES + 2)
#define AGP_USER_UNCACHED_MEMORY (AGP_USER_TYPES + 4)

/* flag for GFDT type */
#define AGP_USER_CACHED_MEMORY_GFDT (1 << 3)

#endif
