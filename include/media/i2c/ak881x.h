/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for AK8813 / AK8814 TV-ecoders from Asahi Kasei Microsystems Co., Ltd. (AKM)
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#ifndef AK881X_H
#define AK881X_H

#define AK881X_IF_MODE_MASK	(3 << 0)
#define AK881X_IF_MODE_BT656	(0 << 0)
#define AK881X_IF_MODE_MASTER	(1 << 0)
#define AK881X_IF_MODE_SLAVE	(2 << 0)
#define AK881X_FIELD		(1 << 2)
#define AK881X_COMPONENT	(1 << 3)

struct ak881x_pdata {
	unsigned long flags;
};

#endif
