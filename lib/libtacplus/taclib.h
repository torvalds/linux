/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001, Juniper Networks, Inc.
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
 *	$FreeBSD$
 */

#ifndef _TACLIB_H_
#define _TACLIB_H_

#include <sys/types.h>

struct tac_handle;

/* Flags for tac_add_server(). */
#define TAC_SRVR_SINGLE_CONNECT	0x04	/* Keep connection open for multiple
					   sessions. */

/* Disassembly of tac_send_authen() return value. */
#define	TAC_AUTHEN_STATUS(s)	((s) & 0xff)
#define TAC_AUTHEN_NOECHO(s)	((s) & (1<<8))

/* Disassembly of tac_send_author() return value. */
#define	TAC_AUTHOR_STATUS(s)	((s) & 0xff)
#define TAC_AUTHEN_AV_COUNT(s)	(((s)>>8) & 0xff)

/* Privilege levels */
#define TAC_PRIV_LVL_MIN	0x00
#define TAC_PRIV_LVL_USER	0x01
#define TAC_PRIV_LVL_ROOT	0x0f
#define TAC_PRIV_LVL_MAX	0x0f

/* Authentication actions */
#define TAC_AUTHEN_LOGIN	0x01
#define TAC_AUTHEN_CHPASS	0x02
#define TAC_AUTHEN_SENDPASS	0x03
#define TAC_AUTHEN_SENDAUTH	0x04

/* Authentication types */
#define TAC_AUTHEN_TYPE_ASCII	0x01
#define TAC_AUTHEN_TYPE_PAP	0x02
#define TAC_AUTHEN_TYPE_CHAP	0x03
#define TAC_AUTHEN_TYPE_ARAP	0x04
#define TAC_AUTHEN_TYPE_MSCHAP	0x05

/* Authentication services */
#define TAC_AUTHEN_SVC_NONE	0x00
#define TAC_AUTHEN_SVC_LOGIN	0x01
#define TAC_AUTHEN_SVC_ENABLE	0x02
#define TAC_AUTHEN_SVC_PPP	0x03
#define TAC_AUTHEN_SVC_ARAP	0x04
#define TAC_AUTHEN_SVC_PT	0x05
#define TAC_AUTHEN_SVC_RCMD	0x06
#define TAC_AUTHEN_SVC_X25	0x07
#define TAC_AUTHEN_SVC_NASI	0x08
#define TAC_AUTHEN_SVC_FWPROXY	0x09

/* Authentication reply status codes */
#define TAC_AUTHEN_STATUS_PASS		0x01
#define TAC_AUTHEN_STATUS_FAIL		0x02
#define TAC_AUTHEN_STATUS_GETDATA	0x03
#define TAC_AUTHEN_STATUS_GETUSER	0x04
#define TAC_AUTHEN_STATUS_GETPASS	0x05
#define TAC_AUTHEN_STATUS_RESTART	0x06
#define TAC_AUTHEN_STATUS_ERROR		0x07
#define TAC_AUTHEN_STATUS_FOLLOW	0x21

/* Authorization authenticatication methods */
#define TAC_AUTHEN_METH_NOT_SET         0x00
#define TAC_AUTHEN_METH_NONE            0x01
#define TAC_AUTHEN_METH_KRB5            0x02
#define TAC_AUTHEN_METH_LINE            0x03
#define TAC_AUTHEN_METH_ENABLE          0x04
#define TAC_AUTHEN_METH_LOCAL           0x05
#define TAC_AUTHEN_METH_TACACSPLUS      0x06
#define TAC_AUTHEN_METH_RCMD            0x20
/* If adding more, see comments in protocol_version() in taclib.c */

/* Authorization status */
#define TAC_AUTHOR_STATUS_PASS_ADD      0x01
#define TAC_AUTHOR_STATUS_PASS_REPL     0x02
#define TAC_AUTHOR_STATUS_FAIL          0x10
#define TAC_AUTHOR_STATUS_ERROR         0x11

/* Accounting actions */
#define TAC_ACCT_MORE			0x1
#define TAC_ACCT_START			0x2
#define TAC_ACCT_STOP			0x4
#define TAC_ACCT_WATCHDOG		0x8

/* Accounting status */
#define TAC_ACCT_STATUS_SUCCESS		0x1
#define TAC_ACCT_STATUS_ERROR		0x2
#define TAC_ACCT_STATUS_FOLLOW		0x21

__BEGIN_DECLS
int			 tac_add_server(struct tac_handle *,
			    const char *, int, const char *, int, int);
void			 tac_close(struct tac_handle *);
int			 tac_config(struct tac_handle *, const char *);
int			 tac_create_authen(struct tac_handle *, int, int, int);
void			*tac_get_data(struct tac_handle *, size_t *);
char			*tac_get_msg(struct tac_handle *);
struct tac_handle	*tac_open(void);
int			 tac_send_authen(struct tac_handle *);
int			 tac_set_data(struct tac_handle *,
			    const void *, size_t);
int			 tac_set_msg(struct tac_handle *, const char *);
int			 tac_set_port(struct tac_handle *, const char *);
int			 tac_set_priv(struct tac_handle *, int);
int			 tac_set_rem_addr(struct tac_handle *, const char *);
int			 tac_set_user(struct tac_handle *, const char *);
const char		*tac_strerror(struct tac_handle *);
int			 tac_send_author(struct tac_handle *);
int			 tac_create_author(struct tac_handle *, int, int, int);
int			 tac_set_av(struct tac_handle *, u_int, const char *);
char			*tac_get_av(struct tac_handle *, u_int);
char			*tac_get_av_value(struct tac_handle *, const char *);
void			 tac_clear_avs(struct tac_handle *);
int			 tac_create_acct(struct tac_handle *, int, int, int, int);
int			 tac_send_acct(struct tac_handle *);
__END_DECLS

#endif /* _TACLIB_H_ */
