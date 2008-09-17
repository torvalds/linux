/*
 * Ultra Wide Band
 * Debug Support
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: doc
 * Invoke like:
 *
 * #define D_LOCAL 4
 * #include <linux/uwb/debug.h>
 *
 * At the end of your include files.
 */
#include <linux/types.h>

struct device;
extern void dump_bytes(struct device *dev, const void *_buf, size_t rsize);

/* Master debug switch; !0 enables, 0 disables */
#define D_MASTER (!0)

/* Local (per-file) debug switch; #define before #including */
#ifndef D_LOCAL
#define D_LOCAL 0
#endif

#undef __d_printf
#undef d_fnstart
#undef d_fnend
#undef d_printf
#undef d_dump

#define __d_printf(l, _tag, _dev, f, a...)				\
do {									\
	struct device *__dev = (_dev);					\
	if (D_MASTER && D_LOCAL >= (l)) {				\
		char __head[64] = "";					\
		if (_dev != NULL) {					\
			if ((unsigned long)__dev < 4096)		\
				printk(KERN_ERR "E: Corrupt dev %p\n",	\
					__dev);				\
			else						\
				snprintf(__head, sizeof(__head),	\
					 "%s %s: ",			\
					 dev_driver_string(__dev),	\
					 __dev->bus_id);		\
		}							\
		printk(KERN_ERR "%s%s" _tag ": " f, __head,		\
			__func__, ## a);				\
	}								\
} while (0 && _dev)

#define d_fnstart(l, _dev, f, a...)	\
	__d_printf(l, " FNSTART", _dev, f, ## a)
#define d_fnend(l, _dev, f, a...)	\
	__d_printf(l, " FNEND", _dev, f, ## a)
#define d_printf(l, _dev, f, a...)	\
	__d_printf(l, "", _dev, f, ## a)
#define d_dump(l, _dev, ptr, size)		\
do {						\
	struct device *__dev = _dev;		\
	if (D_MASTER && D_LOCAL >= (l))		\
		dump_bytes(__dev, ptr, size);	\
} while (0 && _dev)
#define d_test(l) (D_MASTER && D_LOCAL >= (l))
