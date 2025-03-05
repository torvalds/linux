/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_COMMAND_DB_H__
#define __QCOM_COMMAND_DB_H__

#include <linux/err.h>

enum cmd_db_hw_type {
	CMD_DB_HW_INVALID = 0,
	CMD_DB_HW_MIN     = 3,
	CMD_DB_HW_ARC     = CMD_DB_HW_MIN,
	CMD_DB_HW_VRM     = 4,
	CMD_DB_HW_BCM     = 5,
	CMD_DB_HW_MAX     = CMD_DB_HW_BCM,
	CMD_DB_HW_ALL     = 0xff,
};

#if IS_ENABLED(CONFIG_QCOM_COMMAND_DB)
u32 cmd_db_read_addr(const char *resource_id);

const void *cmd_db_read_aux_data(const char *resource_id, size_t *len);

bool cmd_db_match_resource_addr(u32 addr1, u32 addr2);

enum cmd_db_hw_type cmd_db_read_slave_id(const char *resource_id);

int cmd_db_ready(void);

bool cmd_db_is_standalone(void);
#else
static inline u32 cmd_db_read_addr(const char *resource_id)
{ return 0; }

static inline const void *cmd_db_read_aux_data(const char *resource_id, size_t *len)
{ return ERR_PTR(-ENODEV); }

static inline bool cmd_db_match_resource_addr(u32 addr1, u32 addr2)
{ return false; }

static inline enum cmd_db_hw_type cmd_db_read_slave_id(const char *resource_id)
{ return -ENODEV; }

static inline int cmd_db_ready(void)
{ return -ENODEV; }

static inline bool cmd_db_is_standalone(void)
{ return true; }
#endif /* CONFIG_QCOM_COMMAND_DB */
#endif /* __QCOM_COMMAND_DB_H__ */
