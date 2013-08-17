/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	cthw20k2.h
 *
 * @Brief
 * This file contains the definition of hardware access methord.
 *
 * @Author	Liu Chun
 * @Date 	May 13 2008
 *
 */

#ifndef CTHW20K2_H
#define CTHW20K2_H

#include "cthardware.h"

int create_20k2_hw_obj(struct hw **rhw);
int destroy_20k2_hw_obj(struct hw *hw);

#endif /* CTHW20K2_H */
