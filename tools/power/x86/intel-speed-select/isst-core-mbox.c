// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features for Mailbox Interface
 * Copyright (c) 2023 Intel Corporation.
 */
#include "isst.h"

static int mbox_get_disp_freq_multiplier(void)
{
        return DISP_FREQ_MULTIPLIER;
}

static int mbox_get_trl_max_levels(void)
{
        return 3;
}

static char *mbox_get_trl_level_name(int level)
{
        switch (level) {
        case 0:
                return "sse";
        case 1:
                return "avx2";
        case 2:
                return "avx512";
        default:
                return NULL;
        }
}

static int mbox_is_punit_valid(struct isst_id *id)
{
	if (id->cpu < 0)
		return 0;

	if (id->pkg < 0 || id->die < 0 || id->punit)
		return 0;

	return 1;
}

static int mbox_get_config_levels(struct isst_id *id, struct isst_pkg_ctdp *pkg_dev)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_LEVELS_INFO, 0, 0, &resp);
	if (ret) {
		pkg_dev->levels = 0;
		pkg_dev->locked = 1;
		pkg_dev->current_level = 0;
		pkg_dev->version = 0;
		pkg_dev->enabled = 0;
		return 0;
	}

	debug_printf("cpu:%d CONFIG_TDP_GET_LEVELS_INFO resp:%x\n", id->cpu, resp);

	pkg_dev->version = resp & 0xff;
	pkg_dev->levels = (resp >> 8) & 0xff;
	pkg_dev->current_level = (resp >> 16) & 0xff;
	pkg_dev->locked = !!(resp & BIT(24));
	pkg_dev->enabled = !!(resp & BIT(31));

	return 0;
}

static struct isst_platform_ops mbox_ops = {
	.get_disp_freq_multiplier = mbox_get_disp_freq_multiplier,
	.get_trl_max_levels = mbox_get_trl_max_levels,
	.get_trl_level_name = mbox_get_trl_level_name,
	.is_punit_valid = mbox_is_punit_valid,
	.get_config_levels = mbox_get_config_levels,
};

struct isst_platform_ops *mbox_get_platform_ops(void)
{
	return &mbox_ops;
}
