/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Copyright (c) 2018 Microsemi Corporation */
#ifndef __PHY_OCELOT_SERDES_H__
#define __PHY_OCELOT_SERDES_H__

#define SERDES1G(x)	(x)
#define SERDES1G_MAX	SERDES1G(5)
#define SERDES6G(x)	(SERDES1G_MAX + 1 + (x))
#define SERDES6G_MAX	SERDES6G(2)
#define SERDES_MAX	(SERDES6G_MAX + 1)

#endif
