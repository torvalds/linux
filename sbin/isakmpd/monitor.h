/* $OpenBSD: monitor.h,v 1.19 2008/12/22 14:30:04 hshoexer Exp $	 */

/*
 * Copyright (c) 2003 Håkan Olsson.  All rights reserved.
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

#ifndef _MONITOR_H_
#define _MONITOR_H_

#include <sys/types.h>
#include <sys/stat.h>

#define ISAKMPD_PRIVSEP_USER "_isakmpd"

#define ISAKMP_PORT_DEFAULT	500

enum monitor_reqtypes {
	MONITOR_PFKEY_OPEN,
	MONITOR_GET_FD,
	MONITOR_SETSOCKOPT,
	MONITOR_BIND,
	MONITOR_REQ_READDIR,
	MONITOR_MKFIFO,
	MONITOR_INIT_DONE,
	MONITOR_SHUTDOWN
};

pid_t           monitor_init(int);
void            monitor_loop(int);

int             mm_send_fd(int, int);
int             mm_receive_fd(int);

FILE           *monitor_fopen(const char *, const char *);
int             monitor_open(const char *, int, mode_t);
int             monitor_stat(const char *, struct stat *);
int             monitor_setsockopt(int, int, int, const void *, socklen_t);
int             monitor_bind(int, const struct sockaddr *, socklen_t);
int		monitor_req_readdir(const char *);
int		monitor_readdir(char *, size_t);
void            monitor_init_done(void);

int		monitor_pf_key_v2_open(void);
void		monitor_exit(int) __dead;

#endif				/* _MONITOR_H_ */
