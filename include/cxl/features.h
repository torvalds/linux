/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024-2025 Intel Corporation. */
#ifndef __CXL_FEATURES_H__
#define __CXL_FEATURES_H__

#include <linux/uuid.h>
#include <linux/fwctl.h>
#include <uapi/cxl/features.h>

/* Feature UUIDs used by the kernel */
#define CXL_FEAT_PATROL_SCRUB_UUID						\
	UUID_INIT(0x96dad7d6, 0xfde8, 0x482b, 0xa7, 0x33, 0x75, 0x77, 0x4e,	\
		  0x06, 0xdb, 0x8a)

#define CXL_FEAT_ECS_UUID							\
	UUID_INIT(0xe5b13f22, 0x2328, 0x4a14, 0xb8, 0xba, 0xb9, 0x69, 0x1e,	\
		  0x89, 0x33, 0x86)

#define CXL_FEAT_SPPR_UUID							\
	UUID_INIT(0x892ba475, 0xfad8, 0x474e, 0x9d, 0x3e, 0x69, 0x2c, 0x91,	\
		  0x75, 0x68, 0xbb)

#define CXL_FEAT_HPPR_UUID							\
	UUID_INIT(0x80ea4521, 0x786f, 0x4127, 0xaf, 0xb1, 0xec, 0x74, 0x59,	\
		  0xfb, 0x0e, 0x24)

#define CXL_FEAT_CACHELINE_SPARING_UUID						\
	UUID_INIT(0x96C33386, 0x91dd, 0x44c7, 0x9e, 0xcb, 0xfd, 0xaf, 0x65,	\
		  0x03, 0xba, 0xc4)

#define CXL_FEAT_ROW_SPARING_UUID						\
	UUID_INIT(0x450ebf67, 0xb135, 0x4f97, 0xa4, 0x98, 0xc2, 0xd5, 0x7f,	\
		  0x27, 0x9b, 0xed)

#define CXL_FEAT_BANK_SPARING_UUID						\
	UUID_INIT(0x78b79636, 0x90ac, 0x4b64, 0xa4, 0xef, 0xfa, 0xac, 0x5d,	\
		  0x18, 0xa8, 0x63)

#define CXL_FEAT_RANK_SPARING_UUID						\
	UUID_INIT(0x34dbaff5, 0x0552, 0x4281, 0x8f, 0x76, 0xda, 0x0b, 0x5e,	\
		  0x7a, 0x76, 0xa7)

/* Feature commands capability supported by a device */
enum cxl_features_capability {
	CXL_FEATURES_NONE = 0,
	CXL_FEATURES_RO,
	CXL_FEATURES_RW,
};

/**
 * struct cxl_features_state - The Features state for the device
 * @cxlds: Pointer to CXL device state
 * @entries: CXl feature entry context
 */
struct cxl_features_state {
	struct cxl_dev_state *cxlds;
	struct cxl_feat_entries {
		int num_features;
		int num_user_features;
		struct cxl_feat_entry ent[] __counted_by(num_features);
	} *entries;
};

struct cxl_mailbox;
struct cxl_memdev;
#ifdef CONFIG_CXL_FEATURES
struct cxl_features_state *to_cxlfs(struct cxl_dev_state *cxlds);
int devm_cxl_setup_features(struct cxl_dev_state *cxlds);
int devm_cxl_setup_fwctl(struct device *host, struct cxl_memdev *cxlmd);
#else
static inline struct cxl_features_state *to_cxlfs(struct cxl_dev_state *cxlds)
{
	return NULL;
}

static inline int devm_cxl_setup_features(struct cxl_dev_state *cxlds)
{
	return -EOPNOTSUPP;
}

static inline int devm_cxl_setup_fwctl(struct device *host,
				       struct cxl_memdev *cxlmd)
{
	return -EOPNOTSUPP;
}
#endif

#endif
