/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2020 Intel Corporation
 */

/*
 * Intel extended manifest is a extra place to store Intel cavs specific
 * metadata about firmware, for example LPRO/HPRO configuration is
 * Intel cavs specific. This part of output binary is not signed.
 */

#ifndef __INTEL_CAVS_EXT_MANIFEST_H__
#define __INTEL_CAVS_EXT_MANIFEST_H__

#include <sound/sof/ext_manifest.h>

/* EXT_MAN_ELEM_PLATFORM_CONFIG_DATA elements identificators */
enum sof_cavs_config_elem_type {
	SOF_EXT_MAN_CAVS_CONFIG_EMPTY		= 0,
	SOF_EXT_MAN_CAVS_CONFIG_CAVS_LPRO	= 1,
	SOF_EXT_MAN_CAVS_CONFIG_OUTBOX_SIZE	= 2,
	SOF_EXT_MAN_CAVS_CONFIG_INBOX_SIZE	= 3,
};

/* EXT_MAN_ELEM_PLATFORM_CONFIG_DATA elements */
struct sof_ext_man_cavs_config_data {
	struct sof_ext_man_elem_header hdr;

	struct sof_config_elem elems[];
} __packed;

#endif /* __INTEL_CAVS_EXT_MANIFEST_H__ */
