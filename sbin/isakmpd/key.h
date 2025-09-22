/* $OpenBSD: key.h,v 1.8 2005/11/15 22:10:49 cloder Exp $	 */
/*
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * Copyright (c) 2000-2001 Angelos D. Keromytis.
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#ifndef _KEY_H_
#define _KEY_H_

#define ISAKMP_KEY_NONE         0
#define ISAKMP_KEY_PASSPHRASE   1
#define ISAKMP_KEY_RSA          2
#define ISAKMP_KEY_DSA          3

#define ISAKMP_KEYTYPE_PUBLIC   0
#define ISAKMP_KEYTYPE_PRIVATE  1

void            key_free(int, int, void *);
void            key_serialize(int, int, void *, u_int8_t **, size_t *);
char           *key_printable(int, int, u_int8_t *, size_t);
void            key_from_printable(int, int, char *, u_int8_t **, u_int32_t *);
void           *key_internalize(int, int, u_int8_t *, size_t);
#endif				/* _KEY_H_ */
