/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_STRING_H
#define __ASM_AVR32_STRING_H

#define __HAVE_ARCH_MEMSET
extern void *memset(void *b, int c, size_t len);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *to, const void *from, size_t len);

#endif /* __ASM_AVR32_STRING_H */
