/*	$OpenBSD: npppd_auth_local.h,v 1.9 2024/02/26 10:42:05 yasuoka Exp $ */

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

struct _npppd_auth_base {
	/** name of realm */
	char	name[NPPPD_GENERIC_NAME_LEN];
	/** reference indicated to parent npppd */
	npppd 	*npppd;
	/** type of authentication realm */
	int	type;
	/** PPP suffix */
	char	pppsuffix[64];
	uint32_t
		/** whether initialized or not */
		initialized:1,
		/** in disposing */
		disposing:1,
		/** Is the radius configuration ready */
		radius_ready:1,
		/** whether EAP capable or not */
		eap_capable:1,
		/** whether force to strip Windows-NT domain or not */
		strip_nt_domain:1,
		/** whether force to strip after the '@' of PPP username or not */
		strip_atmark_realm:1,
		/** has users list */
		has_users_file:1,
		reserved:25;

	/** path name of account list */
	char	users_file_path[64];
	/** last load time */
	time_t	last_load;
	/**counter of sessions from this auth */
	int    user_max_session;
};

#ifdef USE_NPPPD_RADIUS
struct _npppd_auth_radius {
	/** parent of npppd_auth_base */
	npppd_auth_base nar_base;

	/** RADIUS authentication server setting */
	radius_req_setting *rad_auth_setting;

	/** RADIUS accounting server setting */
	radius_req_setting *rad_acct_setting;

	/** Whether RADIUS accounting-on is noticed */
	int rad_acct_on;
};
#endif

/** type of local authentication realm */
struct _npppd_auth_local {
	/* parent npppd_auth_base */
	npppd_auth_base nal_base;
};

static npppd_auth_user *npppd_auth_get_user (npppd_auth_base *, const char *);
static int              npppd_auth_base_log (npppd_auth_base *, int, const char *, ...);

#ifdef USE_NPPPD_RADIUS
enum RADIUS_SERVER_TYPE {
	RADIUS_SERVER_TYPE_AUTH,
	RADIUS_SERVER_TYPE_ACCT
};

static int              npppd_auth_radius_reload (npppd_auth_base *, struct authconf *);
#endif

#ifdef NPPPD_AUTH_DEBUG
#define NPPPD_AUTH_DBG(x) 	npppd_auth_base_log x
#define NPPPD_AUTH_ASSERT(x)	ASSERT(x)
#else
#define NPPPD_AUTH_DBG(x)
#define NPPPD_AUTH_ASSERT(x)
#endif

#define	DEFAULT_RADIUS_AUTH_PORT	1812
#define	DEFAULT_RADIUS_ACCT_PORT	1813
#define	DEFAULT_RADIUS_TIMEOUT		9
#define	DEFAULT_RADIUS_MAX_TRIES	3
#define	DEFAULT_RADIUS_MAX_FAILOVERS	1

