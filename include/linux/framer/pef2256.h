/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PEF2256 consumer API
 *
 * Copyright 2023 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */
#ifndef __PEF2256_H__
#define __PEF2256_H__

#include <linux/types.h>

struct pef2256;
struct regmap;

/* Retrieve the PEF2256 regmap */
struct regmap *pef2256_get_regmap(struct pef2256 *pef2256);

/* PEF2256 hardware versions */
enum pef2256_version {
	PEF2256_VERSION_UNKNOWN,
	PEF2256_VERSION_1_2,
	PEF2256_VERSION_2_1,
	PEF2256_VERSION_2_2,
};

/* Get the PEF2256 hardware version */
enum pef2256_version pef2256_get_version(struct pef2256 *pef2256);

#endif /* __PEF2256_H__ */
