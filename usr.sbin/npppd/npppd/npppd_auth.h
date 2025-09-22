/*	$OpenBSD: npppd_auth.h,v 1.9 2017/08/11 16:41:47 goda Exp $ */

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
#ifndef	NPPPD_AUTH_H
#define	NPPPD_AUTH_H 1

#include <netinet/in.h>

/** local authentication realm */
#define	NPPPD_AUTH_TYPE_LOCAL		1
/** RADIUS authentication realm */
#define	NPPPD_AUTH_TYPE_RADIUS		2

struct _npppd_auth_base;
struct _npppd_auth_radius;
struct _npppd_auth_local;

/** default type of local authentication realm */
typedef struct _npppd_auth_base npppd_auth_base;

/** type of RADIUS authentication realm */
typedef struct _npppd_auth_radius npppd_auth_radius;
typedef struct _npppd_auth_local npppd_auth_local;

/** the type of user account */
typedef struct _npppd_auth_user {
	/** username */
	char *username;
	/** password */
	char *password;
	/** Framed-IP-Address */
	struct in_addr	framed_ip_address;
	/** Framed-IP-Netmask */
	struct in_addr	framed_ip_netmask;
	/** Calling-Number */
	char *calling_number;
	/** field for space assignment */
	char space[0];
} npppd_auth_user;

#ifdef __cplusplus
extern "C" {
#endif

npppd_auth_base     *npppd_auth_create (int, const char *, void *);
void                npppd_auth_dispose (npppd_auth_base *);
void                npppd_auth_destroy (npppd_auth_base *);
int                 npppd_auth_reload (npppd_auth_base *);
int                 npppd_auth_get_user_password (npppd_auth_base *, const char *, char *, int *);
int                 npppd_auth_get_framed_ip (npppd_auth_base *, const char *, struct in_addr *, struct in_addr *);
int                 npppd_auth_get_calling_number (npppd_auth_base *, const char *, char *, int *);
int                 npppd_auth_get_type (npppd_auth_base *);
int                 npppd_auth_is_usable (npppd_auth_base *);
int                 npppd_auth_is_ready (npppd_auth_base *);
int                 npppd_auth_is_disposing (npppd_auth_base *);
int                 npppd_auth_is_eap_capable (npppd_auth_base *);
const char          *npppd_auth_get_name (npppd_auth_base *);
const char          *npppd_auth_get_suffix (npppd_auth_base *);
const char          *npppd_auth_username_for_auth (npppd_auth_base *, const char *, char *);
void                *npppd_auth_radius_get_radius_auth_setting (npppd_auth_radius *);
void                *npppd_auth_radius_get_radius_acct_setting (npppd_auth_radius *);
int                 npppd_auth_user_session_unlimited(npppd_auth_base *);
int                 npppd_check_auth_user_max_session(npppd_auth_base *, int);

#ifdef __cplusplus
}
#endif
#endif
