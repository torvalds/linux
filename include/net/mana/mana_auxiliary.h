/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022, Microsoft Corporation. */

#include "mana.h"
#include <linux/auxiliary_bus.h>

struct mana_adev {
	struct auxiliary_device adev;
	struct gdma_dev *mdev;
};
