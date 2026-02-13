/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_FEATURES_H
#define _LINUX_VIRTIO_FEATURES_H

#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/string.h>

#define VIRTIO_FEATURES_U64S	2
#define VIRTIO_FEATURES_BITS	(VIRTIO_FEATURES_U64S * 64)

#define VIRTIO_BIT(b)		BIT_ULL((b) & 0x3f)
#define VIRTIO_U64(b)		((b) >> 6)

#define VIRTIO_DECLARE_FEATURES(name)			\
	union {						\
		u64 name;				\
		u64 name##_array[VIRTIO_FEATURES_U64S];\
	}

static inline bool virtio_features_chk_bit(unsigned int bit)
{
	if (__builtin_constant_p(bit)) {
		/*
		 * Don't care returning the correct value: the build
		 * will fail before any bad features access
		 */
		BUILD_BUG_ON(bit >= VIRTIO_FEATURES_BITS);
	} else {
		if (WARN_ON_ONCE(bit >= VIRTIO_FEATURES_BITS))
			return false;
	}
	return true;
}

static inline bool virtio_features_test_bit(const u64 *features,
					    unsigned int bit)
{
	return virtio_features_chk_bit(bit) &&
	       !!(features[VIRTIO_U64(bit)] & VIRTIO_BIT(bit));
}

static inline void virtio_features_set_bit(u64 *features,
					   unsigned int bit)
{
	if (virtio_features_chk_bit(bit))
		features[VIRTIO_U64(bit)] |= VIRTIO_BIT(bit);
}

static inline void virtio_features_clear_bit(u64 *features,
					     unsigned int bit)
{
	if (virtio_features_chk_bit(bit))
		features[VIRTIO_U64(bit)] &= ~VIRTIO_BIT(bit);
}

static inline void virtio_features_zero(u64 *features)
{
	memset(features, 0, sizeof(features[0]) * VIRTIO_FEATURES_U64S);
}

static inline void virtio_features_from_u64(u64 *features, u64 from)
{
	virtio_features_zero(features);
	features[0] = from;
}

static inline bool virtio_features_equal(const u64 *f1, const u64 *f2)
{
	int i;

	for (i = 0; i < VIRTIO_FEATURES_U64S; ++i)
		if (f1[i] != f2[i])
			return false;
	return true;
}

static inline void virtio_features_copy(u64 *to, const u64 *from)
{
	memcpy(to, from, sizeof(to[0]) * VIRTIO_FEATURES_U64S);
}

static inline void virtio_features_andnot(u64 *to, const u64 *f1, const u64 *f2)
{
	int i;

	for (i = 0; i < VIRTIO_FEATURES_U64S; i++)
		to[i] = f1[i] & ~f2[i];
}

#endif
