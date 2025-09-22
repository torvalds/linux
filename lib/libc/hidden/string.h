/*	$OpenBSD: string.h,v 1.6 2018/01/18 08:23:44 guenther Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LIBC_STRING_H_
#define	_LIBC_STRING_H_

#include_next <string.h>

__BEGIN_HIDDEN_DECLS
char	*__strsignal(int , char *);
__END_HIDDEN_DECLS

PROTO_NORMAL(bcmp);
PROTO_NORMAL(bcopy);
PROTO_NORMAL(bzero);
PROTO_NORMAL(explicit_bzero);
PROTO_PROTECTED(ffs);
PROTO_DEPRECATED(index);
PROTO_NORMAL(memccpy);
PROTO_NORMAL(memchr);
PROTO_NORMAL(memcmp);
/*PROTO_NORMAL(memcpy);			use declaration from namespace.h */
PROTO_NORMAL(memmem);
/*PROTO_NORMAL(memmove);		use declaration from namespace.h */
PROTO_NORMAL(memrchr);
/*PROTO_NORMAL(memset);			use declaration from namespace.h */
PROTO_DEPRECATED(rindex);
PROTO_DEPRECATED(stpcpy);
PROTO_NORMAL(stpncpy);
PROTO_NORMAL(strcasecmp);
PROTO_DEPRECATED(strcasecmp_l);
PROTO_NORMAL(strcasestr);
PROTO_STD_DEPRECATED(strcat);
PROTO_NORMAL(strchr);
PROTO_NORMAL(strcmp);
PROTO_NORMAL(strcoll);
PROTO_DEPRECATED(strcoll_l);
PROTO_STD_DEPRECATED(strcpy);
PROTO_NORMAL(strcspn);
PROTO_NORMAL(strdup);
PROTO_NORMAL(strerror);
PROTO_DEPRECATED(strerror_l);
PROTO_NORMAL(strerror_r);
PROTO_NORMAL(strlcat);
PROTO_NORMAL(strlcpy);
PROTO_NORMAL(strlen);
PROTO_NORMAL(strmode);
PROTO_NORMAL(strncasecmp);
PROTO_DEPRECATED(strncasecmp_l);
PROTO_NORMAL(strncat);
PROTO_NORMAL(strncmp);
PROTO_NORMAL(strncpy);
PROTO_NORMAL(strndup);
PROTO_NORMAL(strnlen);
PROTO_NORMAL(strpbrk);
PROTO_NORMAL(strrchr);
PROTO_NORMAL(strsep);
PROTO_NORMAL(strsignal);
PROTO_NORMAL(strspn);
PROTO_NORMAL(strstr);
PROTO_NORMAL(strtok);
PROTO_NORMAL(strtok_r);
PROTO_NORMAL(strxfrm);
PROTO_DEPRECATED(strxfrm_l);
PROTO_NORMAL(timingsafe_bcmp);
PROTO_NORMAL(timingsafe_memcmp);

#endif /* _LIBC_STRING_H_ */
