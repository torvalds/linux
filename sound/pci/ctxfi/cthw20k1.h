/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * @File	cthw20k1.h
 *
 * @Brief
 * This file contains the definition of hardware access methord.
 *
 * @Author	Liu Chun
 * @Date 	May 13 2008
 */

#ifndef CTHW20K1_H
#define CTHW20K1_H

#include "cthardware.h"

int create_20k1_hw_obj(struct hw **rhw);
int destroy_20k1_hw_obj(struct hw *hw);

#endif /* CTHW20K1_H */
