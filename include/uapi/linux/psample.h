/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UAPI_PSAMPLE_H
#define __UAPI_PSAMPLE_H

enum {
	/* sampled packet metadata */
	PSAMPLE_ATTR_IIFINDEX,
	PSAMPLE_ATTR_OIFINDEX,
	PSAMPLE_ATTR_ORIGSIZE,
	PSAMPLE_ATTR_SAMPLE_GROUP,
	PSAMPLE_ATTR_GROUP_SEQ,
	PSAMPLE_ATTR_SAMPLE_RATE,
	PSAMPLE_ATTR_DATA,

	/* commands attributes */
	PSAMPLE_ATTR_GROUP_REFCOUNT,

	__PSAMPLE_ATTR_MAX
};

enum psample_command {
	PSAMPLE_CMD_SAMPLE,
	PSAMPLE_CMD_GET_GROUP,
	PSAMPLE_CMD_NEW_GROUP,
	PSAMPLE_CMD_DEL_GROUP,
};

/* Can be overridden at runtime by module option */
#define PSAMPLE_ATTR_MAX (__PSAMPLE_ATTR_MAX - 1)

#define PSAMPLE_NL_MCGRP_CONFIG_NAME "config"
#define PSAMPLE_NL_MCGRP_SAMPLE_NAME "packets"
#define PSAMPLE_GENL_NAME "psample"
#define PSAMPLE_GENL_VERSION 1

#endif
