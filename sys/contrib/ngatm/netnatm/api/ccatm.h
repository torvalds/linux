/*
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Begemot: libunimsg/netnatm/api/ccatm.h,v 1.1 2004/07/08 08:21:58 brandt Exp $
 *
 * ATM API as defined per af-saa-0108
 *
 * Interface to the supporting code.
 */

#ifndef _API_CCATM_H_
#define _API_CCATM_H_

struct ccuser;
struct ccconn;
struct ccport;
struct ccdata;

struct cc_funcs {
	/* send signal to API user */
	void	(*send_user)(struct ccuser *, void *, u_int, void *, size_t);

	/* respond API user */
	void	(*respond_user)(struct ccuser *, void *, int, u_int,
		    void *, size_t);

	/* send signal to uni for connection */
	void	(*send_uni)(struct ccconn *, void *, u_int, u_int,
		    struct uni_msg *);

	/* send global signal to uni */
	void	(*send_uni_glob)(struct ccport *, void *, u_int, u_int,
		    struct uni_msg *);

	/* log a message */
	void	(*log)(const char *, ...);
};

enum {
	CCLOG_USER_STATE	= 0x00000001,
	CCLOG_USER_INST		= 0x00000002,
	CCLOG_USER_SIG		= 0x00000004,
	CCLOG_CONN_STATE	= 0x00000010,
	CCLOG_CONN_INST		= 0x00000020,
	CCLOG_CONN_SIG		= 0x00000040,
	CCLOG_PARTY_STATE	= 0x00000100,
	CCLOG_PARTY_INST	= 0x00000200,
	CCLOG_PARTY_SIG		= 0x00000400,
	CCLOG_SIGS		= 0x00001000,
};

/* instance handling */
struct ccdata *cc_create(const struct cc_funcs *);
void cc_destroy(struct ccdata *);
void cc_reset(struct ccdata *);

/* input a response from the UNI layer to CC */
int cc_uni_response(struct ccport *, u_int cookie, u_int reason, u_int state);

/* Signal from UNI on this port */
int cc_uni_signal(struct ccport *, u_int cookie, u_int sig, struct uni_msg *);

/* retrieve addresses */
int cc_get_addrs(struct ccdata *, u_int, struct uni_addr **, u_int **, u_int *);

/* dump state */
typedef int (*cc_dump_f)(struct ccdata *, void *, const char *);
int cc_dump(struct ccdata *, size_t, cc_dump_f, void *);

/* start/stop port */
int cc_port_stop(struct ccdata *, u_int);
int cc_port_start(struct ccdata *, u_int);

/* is port running? */
int cc_port_isrunning(struct ccdata *, u_int, int *);

/* return port number */
u_int cc_port_no(struct ccport *);

/* Clear address and prefix information from the named port. */
int cc_port_clear(struct ccdata *, u_int);

/* Address registered.  */
int cc_addr_register(struct ccdata *, u_int, const struct uni_addr *);

/* Address unregistered. */
int cc_addr_unregister(struct ccdata *, u_int, const struct uni_addr *);

/* get port info */
int cc_port_get_param(struct ccdata *, u_int, struct atm_port_info *);

/* set port info */
int cc_port_set_param(struct ccdata *, const struct atm_port_info *);

/* get port list */
int cc_port_getlist(struct ccdata *, u_int *, u_int **);

/* create a port */
struct ccport *cc_port_create(struct ccdata *, void *, u_int);

/* destroy a port */
void cc_port_destroy(struct ccport *, int);

/* New endpoint created */
struct ccuser *cc_user_create(struct ccdata *, void *, const char *);

/* destroy user endpoint */
void cc_user_destroy(struct ccuser *);

/* signal from user */
int cc_user_signal(struct ccuser *, u_int, struct uni_msg *);

/* Management is given up on this node. */
void cc_unmanage(struct ccdata *);

/* handle all queued signals */
void cc_work(struct ccdata *);

/* set/get logging flags */
void cc_set_log(struct ccdata *, u_int);
u_int cc_get_log(const struct ccdata *);

/* get extended status */
int cc_get_extended_status(const struct ccdata *, struct atm_exstatus *,
    struct atm_exstatus_ep **, struct atm_exstatus_port **,
    struct atm_exstatus_conn **, struct atm_exstatus_party **);

#endif
