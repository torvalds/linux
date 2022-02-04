// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */

#include <linux/types.h>

struct cxl_dev_state;
bool cxl_dvsec_decode_init(struct cxl_dev_state *cxlds)
{
	return true;
}
