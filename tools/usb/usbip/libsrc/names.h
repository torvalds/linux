/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *      names.h  --  USB name database manipulation routines
 *
 *      Copyright (C) 1999, 2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	Copyright (C) 2005 Takahiro Hirofuchi
 *	       - names_free() is added.
 */

#ifndef _NAMES_H
#define _NAMES_H

#include <sys/types.h>

/* used by usbip_common.c */
extern const char *names_vendor(u_int16_t vendorid);
extern const char *names_product(u_int16_t vendorid, u_int16_t productid);
extern const char *names_class(u_int8_t classid);
extern const char *names_subclass(u_int8_t classid, u_int8_t subclassid);
extern const char *names_protocol(u_int8_t classid, u_int8_t subclassid,
				  u_int8_t protocolid);
extern int  names_init(char *n);
extern void names_free(void);

#endif /* _NAMES_H */
