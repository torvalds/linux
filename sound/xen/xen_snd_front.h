/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_SND_FRONT_H
#define __XEN_SND_FRONT_H

#include "xen_snd_front_cfg.h"

struct xen_snd_front_info {
	struct xenbus_device *xb_dev;

	struct xen_front_cfg_card cfg;
};

#endif /* __XEN_SND_FRONT_H */
