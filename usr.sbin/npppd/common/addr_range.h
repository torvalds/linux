/*	$OpenBSD: addr_range.h,v 1.4 2017/05/30 17:22:00 yasuoka Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */
#ifndef ADDR_RANGE_H
#define ADDR_RANGE_H

struct in_addr_range {
	u_int32_t   addr;		// !! host byte order
	u_int32_t   mask;		// !! Host byte order
	struct in_addr_range *next;
};


#ifdef __cplusplus
extern "C" {
#endif

struct in_addr_range  *in_addr_range_create(void);
void                  in_addr_range_destroy(struct in_addr_range *);
void                  in_addr_range_list_remove_all(struct in_addr_range **);
int                   in_addr_range_list_includes(struct in_addr_range **, struct in_addr *);
int                   in_addr_range_list_add(struct in_addr_range **, const char *);
int                   main(int, char *[]);

#ifdef __cplusplus
}
#endif

#endif
