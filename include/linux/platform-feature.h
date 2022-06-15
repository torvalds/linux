/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PLATFORM_FEATURE_H
#define _PLATFORM_FEATURE_H

#include <linux/bitops.h>
#include <asm/platform-feature.h>

/* The platform features are starting with the architecture specific ones. */

/* Used to enable platform specific DMA handling for virtio devices. */
#define PLATFORM_VIRTIO_RESTRICTED_MEM_ACCESS	(0 + PLATFORM_ARCH_FEAT_N)

#define PLATFORM_FEAT_N				(1 + PLATFORM_ARCH_FEAT_N)

void platform_set(unsigned int feature);
void platform_clear(unsigned int feature);
bool platform_has(unsigned int feature);

#endif /* _PLATFORM_FEATURE_H */
