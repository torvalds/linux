/*
 *  linux/include/asm-arm/bugs.h
 *
 *  Copyright (C) 1995-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_BUGS_H
#define __ASM_BUGS_H

extern void check_writebuffer_bugs(void);

#define check_bugs() check_writebuffer_bugs()

#endif
