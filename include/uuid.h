/*	$OpenBSD: uuid.h,v 1.1 2014/08/31 09:36:36 miod Exp $	*/
/*	$NetBSD: uuid.h,v 1.2 2008/04/23 07:52:32 plunky Exp $	*/

/*
 * Copyright (c) 2002 Marcel Moolenaar
 * Copyright (c) 2002 Hiten Mahesh Pandya
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/include/uuid.h,v 1.2 2002/11/05 10:55:16 jmallett Exp $
 */

#ifndef _UUID_H_
#define	_UUID_H_

#include <sys/types.h>
#include <sys/uuid.h>

/* Status codes returned by the functions. */
#define	uuid_s_ok			0
#define	uuid_s_bad_version		1
#define	uuid_s_invalid_string_uuid	2
#define	uuid_s_no_memory		3

/* Length of a printed UUID. */
#define UUID_BUF_LEN		_UUID_BUF_LEN

/* Length of a UUID. */
#define	UUID_STR_LEN		36

#include <sys/cdefs.h>

__BEGIN_DECLS
int32_t	uuid_compare(const uuid_t *, const uuid_t *, uint32_t *);
void	uuid_create(uuid_t *, uint32_t *);
void	uuid_create_nil(uuid_t *, uint32_t *);
int32_t	uuid_equal(const uuid_t *, const uuid_t *, uint32_t *);
void	uuid_from_string(const char *, uuid_t *, uint32_t *);
uint16_t uuid_hash(const uuid_t *, uint32_t *);
int32_t	uuid_is_nil(const uuid_t *, uint32_t *);
void	uuid_to_string(const uuid_t *, char **, uint32_t *);

void	uuid_enc_le(void *, const uuid_t *);
void	uuid_dec_le(const void *, uuid_t *);
void	uuid_enc_be(void *, const uuid_t *);
void	uuid_dec_be(const void *, uuid_t *);
__END_DECLS

#endif	/* _UUID_H_ */
