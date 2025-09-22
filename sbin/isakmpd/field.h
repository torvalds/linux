/* $OpenBSD: field.h,v 1.6 2004/05/23 18:17:55 hshoexer Exp $	 */
/* $EOM: field.h,v 1.3 1998/08/02 20:25:01 niklas Exp $	 */

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

#ifndef _FIELD_H_
#define _FIELD_H_

#include <sys/types.h>

struct field {
	char	*name;
	int	 offset;
	size_t	 len;
	enum {
		 raw, num, mask, ign, cst
	}	 type;
	struct constant_map **maps;
};

extern void     field_dump_field(struct field *, u_int8_t *);
extern void     field_dump_payload(struct field *, u_int8_t *);
extern u_int32_t field_get_num(struct field *, u_int8_t *);
extern void     field_get_raw(struct field *, u_int8_t *, u_int8_t *);
extern void     field_set_num(struct field *, u_int8_t *, u_int32_t);
extern void     field_set_raw(struct field *, u_int8_t *, u_int8_t *);

#endif				/* _FIELD_H_ */
