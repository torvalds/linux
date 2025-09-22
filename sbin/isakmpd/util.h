/* $OpenBSD: util.h,v 1.33 2017/12/05 20:31:45 jca Exp $	 */
/* $EOM: util.h,v 1.10 2000/10/24 13:33:39 niklas Exp $	 */

/*
 * Copyright (c) 1998 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2001, 2004 Håkan Olsson.  All rights reserved.
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

#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/types.h>
#include <sys/time.h>

extern int      allow_name_lookups;

extern int      sysdep_cleartext(int, int);

struct message;
struct sockaddr;

extern int      check_file_secrecy_fd(int, char *, size_t *);
extern u_int16_t decode_16(u_int8_t *);
extern u_int32_t decode_32(u_int8_t *);
extern void     encode_16(u_int8_t *, u_int16_t);
extern void     encode_32(u_int8_t *, u_int32_t);
extern int      hex2raw(char *, u_int8_t *, size_t);
extern char 	*raw2hex(u_int8_t *, size_t);
extern int      sockaddr2text(struct sockaddr *, char **, int);
extern u_int8_t *sockaddr_addrdata(struct sockaddr *);
extern int      sockaddr_addrlen(struct sockaddr *);
extern in_port_t sockaddr_port(struct sockaddr *);
extern void	sockaddr_set_port(struct sockaddr *, in_port_t);
extern in_port_t text2port(char *);
extern int      text2sockaddr(char *, char *, struct sockaddr **,
		    sa_family_t, int);
extern void     util_ntoa(char **, int, u_int8_t *);
extern int      zero_test(const u_int8_t *, size_t);
extern long	get_timeout(struct timespec *);
extern int	expand_string(char *, size_t, const char *, const char *);

#endif				/* _UTIL_H_ */
