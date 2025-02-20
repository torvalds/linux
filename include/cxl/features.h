/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024-2025 Intel Corporation. */
#ifndef __CXL_FEATURES_H__
#define __CXL_FEATURES_H__

#include <linux/uuid.h>

/* Feature commands capability supported by a device */
enum cxl_features_capability {
	CXL_FEATURES_NONE = 0,
	CXL_FEATURES_RO,
	CXL_FEATURES_RW,
};

/* Get Supported Features (0x500h) CXL r3.2 8.2.9.6.1 */
struct cxl_mbox_get_sup_feats_in {
	__le32 count;
	__le16 start_idx;
	u8 reserved[2];
} __packed;

/* CXL spec r3.2 Table 8-87 command effects */
#define CXL_CMD_CONFIG_CHANGE_COLD_RESET	BIT(0)
#define CXL_CMD_CONFIG_CHANGE_IMMEDIATE		BIT(1)
#define CXL_CMD_DATA_CHANGE_IMMEDIATE		BIT(2)
#define CXL_CMD_POLICY_CHANGE_IMMEDIATE		BIT(3)
#define CXL_CMD_LOG_CHANGE_IMMEDIATE		BIT(4)
#define CXL_CMD_SECURITY_STATE_CHANGE		BIT(5)
#define CXL_CMD_BACKGROUND			BIT(6)
#define CXL_CMD_BGCMD_ABORT_SUPPORTED		BIT(7)
#define CXL_CMD_EFFECTS_VALID			BIT(9)
#define CXL_CMD_CONFIG_CHANGE_CONV_RESET	BIT(10)
#define CXL_CMD_CONFIG_CHANGE_CXL_RESET		BIT(11)

/*
 * CXL spec r3.2 Table 8-109
 * Get Supported Features Supported Feature Entry
 */
struct cxl_feat_entry {
	uuid_t uuid;
	__le16 id;
	__le16 get_feat_size;
	__le16 set_feat_size;
	__le32 flags;
	u8 get_feat_ver;
	u8 set_feat_ver;
	__le16 effects;
	u8 reserved[18];
} __packed;

/* @flags field for 'struct cxl_feat_entry' */
#define CXL_FEATURE_F_CHANGEABLE		BIT(0)
#define CXL_FEATURE_F_PERSIST_FW_UPDATE		BIT(4)
#define CXL_FEATURE_F_DEFAULT_SEL		BIT(5)
#define CXL_FEATURE_F_SAVED_SEL			BIT(6)

/*
 * CXL spec r3.2 Table 8-108
 * Get supported Features Output Payload
 */
struct cxl_mbox_get_sup_feats_out {
	__struct_group(cxl_mbox_get_sup_feats_out_hdr, hdr, /* no attrs */,
		__le16 num_entries;
		__le16 supported_feats;
		__u8 reserved[4];
	);
	struct cxl_feat_entry ents[] __counted_by_le(num_entries);
} __packed;

/**
 * struct cxl_features_state - The Features state for the device
 * @cxlds: Pointer to CXL device state
 * @entries: CXl feature entry context
 *	@num_features: total Features supported by the device
 *	@ent: Flex array of Feature detail entries from the device
 */
struct cxl_features_state {
	struct cxl_dev_state *cxlds;
	struct cxl_feat_entries {
		int num_features;
		struct cxl_feat_entry ent[] __counted_by(num_features);
	} *entries;
};

#ifdef CONFIG_CXL_FEATURES
inline struct cxl_features_state *to_cxlfs(struct cxl_dev_state *cxlds);
int devm_cxl_setup_features(struct cxl_dev_state *cxlds);
#else
static inline struct cxl_features_state *to_cxlfs(struct cxl_dev_state *cxlds)
{
	return NULL;
}

static inline int devm_cxl_setup_features(struct cxl_dev_state *cxlds)
{
	return -EOPNOTSUPP;
}
#endif

#endif
