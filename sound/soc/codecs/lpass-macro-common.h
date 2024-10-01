/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 */

#ifndef __LPASS_MACRO_COMMON_H__
#define __LPASS_MACRO_COMMON_H__

/* NPL clock is expected */
#define LPASS_MACRO_FLAG_HAS_NPL_CLOCK		BIT(0)
/* The soundwire block should be internally reset at probe */
#define LPASS_MACRO_FLAG_RESET_SWR		BIT(1)

enum lpass_version {
	LPASS_VER_9_0_0,
	LPASS_VER_9_2_0,
	LPASS_VER_10_0_0,
	LPASS_VER_11_0_0,
};

enum lpass_codec_version {
	LPASS_CODEC_VERSION_UNKNOWN,
	LPASS_CODEC_VERSION_1_0,
	LPASS_CODEC_VERSION_1_1,
	LPASS_CODEC_VERSION_1_2,
	LPASS_CODEC_VERSION_2_0,
	LPASS_CODEC_VERSION_2_1,
	LPASS_CODEC_VERSION_2_5,
	LPASS_CODEC_VERSION_2_6,
	LPASS_CODEC_VERSION_2_7,
	LPASS_CODEC_VERSION_2_8,
};

struct lpass_macro {
	struct device *macro_pd;
	struct device *dcodec_pd;
};

struct lpass_macro *lpass_macro_pds_init(struct device *dev);
void lpass_macro_pds_exit(struct lpass_macro *pds);
void lpass_macro_set_codec_version(enum lpass_codec_version version);
enum lpass_codec_version lpass_macro_get_codec_version(void);

static inline void lpass_macro_pds_exit_action(void *pds)
{
	lpass_macro_pds_exit(pds);
}

static inline const char *lpass_macro_get_codec_version_string(int version)
{
	switch (version) {
	case LPASS_CODEC_VERSION_1_0:
		return "v1.0";
	case LPASS_CODEC_VERSION_1_1:
		return "v1.1";
	case LPASS_CODEC_VERSION_1_2:
		return "v1.2";
	case LPASS_CODEC_VERSION_2_0:
		return "v2.0";
	case LPASS_CODEC_VERSION_2_1:
		return "v2.1";
	case LPASS_CODEC_VERSION_2_5:
		return "v2.5";
	case LPASS_CODEC_VERSION_2_6:
		return "v2.6";
	case LPASS_CODEC_VERSION_2_7:
		return "v2.7";
	case LPASS_CODEC_VERSION_2_8:
		return "v2.8";
	default:
		break;
	}
	return "NA";
}

#endif /* __LPASS_MACRO_COMMON_H__ */
