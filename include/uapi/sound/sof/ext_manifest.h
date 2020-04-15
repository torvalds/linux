/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
 */

/*
 * Extended manifest is a place to store metadata about firmware, known during
 * compilation time - for example firmware version or used compiler.
 * Given information are read on host side before firmware startup.
 * This part of output binary is not signed.
 */

#ifndef __SOF_FIRMWARE_EXT_MANIFEST_H__
#define __SOF_FIRMWARE_EXT_MANIFEST_H__

#include <linux/const.h>

/* In ASCII `XMan` */
#define SOF_EXT_MAN_MAGIC_NUMBER	0x6e614d58

/* Build u32 number in format MMmmmppp */
#define SOF_EXT_MAN_BUILD_VERSION(MAJOR, MINOR, PATH) ((uint32_t)( \
	((MAJOR) << 24) | \
	((MINOR) << 12) | \
	(PATH)))

/* check extended manifest version consistency */
#define SOF_EXT_MAN_VERSION_INCOMPATIBLE(host_ver, cli_ver) ( \
	((host_ver) & GENMASK(31, 24)) != \
	((cli_ver) & GENMASK(31, 24)))

/* used extended manifest header version */
#define SOF_EXT_MAN_VERSION		SOF_EXT_MAN_BUILD_VERSION(1, 0, 0)

/* extended manifest header, deleting any field breaks backward compatibility */
struct sof_ext_man_header {
	uint32_t magic;		/*< identification number, */
				/*< EXT_MAN_MAGIC_NUMBER */
	uint32_t full_size;	/*< [bytes] full size of ext_man, */
				/*< (header + content + padding) */
	uint32_t header_size;	/*< [bytes] makes header extensionable, */
				/*< after append new field to ext_man header */
				/*< then backward compatible won't be lost */
	uint32_t header_version; /*< value of EXT_MAN_VERSION */
				/*< not related with following content */
	uint8_t elements[];	/*< list of ext_man_elem_* elements */
} __packed;

/* Now define extended manifest elements */

/* Extended manifest elements types */
enum sof_ext_man_elem_type {
	SOF_EXT_MAN_ELEM_FW_VERSION		= 0,
};

/* extended manifest element header */
struct sof_ext_man_elem_header {
	uint32_t type;		/*< SOF_EXT_MAN_ELEM_ */
	uint32_t size;		/*< in bytes, including header size */
	uint8_t blob[];		/*< type dependent content */
} __packed;

/* FW version */
struct sof_ext_man_fw_version {
	struct sof_ext_man_elem_header hdr;
	/* use sof_ipc struct because of code re-use */
	struct sof_ipc_fw_version version;
	uint32_t flags;
} __packed;

#endif /* __SOF_FIRMWARE_EXT_MANIFEST_H__ */
