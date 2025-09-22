/* $OpenBSD: attribute.h,v 1.6 2004/05/14 08:42:56 hshoexer Exp $	 */
/* $EOM: attribute.h,v 1.2 1998/09/29 21:51:07 niklas Exp $	 */

/*
 * Copyright (c) 1998 Niklas Hallqvist.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#ifndef _ATTRIBUTE_H_
#define _ATTRIBUTE_H_

#include <sys/types.h>

struct constant_map;

extern int	 attribute_map(u_int8_t *, size_t, int (*)(u_int16_t,
		     u_int8_t *, u_int16_t, void *), void *);
extern u_int8_t	*attribute_set_basic(u_int8_t *, u_int16_t, u_int16_t);
extern int	 attribute_set_constant(char *, char *, struct constant_map *,
		     int, u_int8_t **);
extern u_int8_t	*attribute_set_var(u_int8_t *, u_int16_t, u_int8_t *,
		     u_int16_t);

#endif				/* _ATTRIBUTE_H_ */
