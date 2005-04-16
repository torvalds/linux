/*
 * hvconsole.h
 * Copyright (C) 2004 Ryan S Arnold, IBM Corporation
 *
 * LPAR console support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _PPC64_HVCONSOLE_H
#define _PPC64_HVCONSOLE_H

/*
 * This is the max number of console adapters that can/will be found as
 * console devices on first stage console init.  Any number beyond this range
 * can't be used as a console device but is still a valid tty device.
 */
#define MAX_NR_HVC_CONSOLES	16

extern int hvc_get_chars(uint32_t vtermno, char *buf, int count);
extern int hvc_put_chars(uint32_t vtermno, const char *buf, int count);

/* Early discovery of console adapters. */
extern int hvc_find_vtys(void);

/* Implemented by a console driver */
extern int hvc_instantiate(uint32_t vtermno, int index);
#endif /* _PPC64_HVCONSOLE_H */
