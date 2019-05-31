/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    cs53l32a.h - definition for cs53l32a inputs and outputs

    Copyright (C) 2006 Hans Verkuil (hverkuil@xs4all.nl)

*/

#ifndef _CS53L32A_H_
#define _CS53L32A_H_

/* There are 2 physical inputs, but the second input can be
   placed in two modes, the first mode bypasses the PGA (gain),
   the second goes through the PGA. Hence there are three
   possible inputs to choose from. */

/* CS53L32A HW inputs */
#define CS53L32A_IN0 0
#define CS53L32A_IN1 1
#define CS53L32A_IN2 2

#endif
