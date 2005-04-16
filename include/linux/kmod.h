#ifndef __LINUX_KMOD_H__
#define __LINUX_KMOD_H__

/*
 *	include/linux/kmod.h
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/compiler.h>

#define KMOD_PATH_LEN 256

#ifdef CONFIG_KMOD
/* modprobe exit status on success, -ve on error.  Return value
 * usually useless though. */
extern int request_module(const char * name, ...) __attribute__ ((format (printf, 1, 2)));
#else
static inline int request_module(const char * name, ...) { return -ENOSYS; }
#endif

#define try_then_request_module(x, mod...) ((x) ?: (request_module(mod), (x)))
extern int call_usermodehelper(char *path, char *argv[], char *envp[], int wait);
extern void usermodehelper_init(void);

#endif /* __LINUX_KMOD_H__ */
