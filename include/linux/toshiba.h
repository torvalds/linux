/* toshiba.h -- Linux driver for accessing the SMM on Toshiba laptops 
 *
 * Copyright (c) 1996-2000  Jonathan A. Buzzard (jonathan@buzzard.org.uk)
 *
 * Thanks to Juergen Heinzl <juergen@monocerus.demon.co.uk> for the pointers
 * on making sure the structure is aligned and packed.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#ifndef _LINUX_TOSHIBA_H
#define _LINUX_TOSHIBA_H

#include <uapi/linux/toshiba.h>

int tosh_smm(SMMRegisters *regs);
#endif
