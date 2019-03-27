/* $OpenBSD: xmalloc.h,v 1.4 2015/11/12 16:30:30 mmcc Exp $ */

/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Created: Mon Mar 20 22:09:17 1995 ylo
 *
 * Versions of malloc and friends that check their results, and never return
 * failure (they call fatal if they encounter an error).
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *
 * $FreeBSD$
 */

#ifndef XMALLOC_H
#define XMALLOC_H

void	*xmalloc(size_t);
void	*xcalloc(size_t, size_t);
void	*xreallocarray(void *, size_t, size_t);
char	*xstrdup(const char *);
int	 xasprintf(char **, const char *, ...)
                __attribute__((__format__ (printf, 2, 3)))
                __attribute__((__nonnull__ (2)));

#endif	/* XMALLOC_H */
