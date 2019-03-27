/*-
 * Copyright (c) 1996 by
 * Sean Eric Fagan <sef@kithrup.com>
 * David Nugent <davidn@blaze.net.au>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. This work was done expressly for inclusion into FreeBSD.  Other use
 *    is permitted provided this notation is included.
 * 4. Absolutely no warranty of function or purpose is made by the authors.
 * 5. Modifications may be freely made to this file providing the above
 *    conditions are met.
 *
 * Low-level routines relating to the user capabilities database
 *
 *	Was login_cap.h,v 1.9 1997/05/07 20:00:01 eivind Exp
 * $FreeBSD$
 */

#ifndef _LOGIN_CAP_H_
#define _LOGIN_CAP_H_

#define LOGIN_DEFCLASS		"default"
#define LOGIN_DEFROOTCLASS	"root"
#define LOGIN_MECLASS		"me"
#define LOGIN_DEFSTYLE		"passwd"
#define LOGIN_DEFSERVICE	"login"
#define LOGIN_DEFUMASK		022
#define LOGIN_DEFPRI		0
#define _PATH_LOGIN_CONF	"/etc/login.conf"
#define _FILE_LOGIN_CONF	".login_conf"
#define _PATH_AUTHPROG		"/usr/libexec/login_"

#define LOGIN_SETGROUP		0x0001		/* set group */
#define LOGIN_SETLOGIN		0x0002		/* set login (via setlogin) */
#define LOGIN_SETPATH		0x0004		/* set path */
#define LOGIN_SETPRIORITY	0x0008		/* set priority */
#define LOGIN_SETRESOURCES	0x0010		/* set resources (cputime, etc.) */
#define LOGIN_SETUMASK		0x0020		/* set umask, obviously */
#define LOGIN_SETUSER		0x0040		/* set user (via setuid) */
#define LOGIN_SETENV		0x0080		/* set user environment */
#define LOGIN_SETMAC		0x0100		/* set user default MAC label */
#define LOGIN_SETCPUMASK	0x0200		/* set user cpumask */
#define LOGIN_SETLOGINCLASS	0x0400		/* set login class in the kernel */
#define LOGIN_SETALL		0x07ff		/* set everything */

#define BI_AUTH		"authorize"		/* accepted authentication */
#define BI_REJECT	"reject"		/* rejected authentication */
#define BI_CHALLENG	"reject challenge"	/* reject with a challenge */
#define BI_SILENT	"reject silent"		/* reject silently */
#define BI_REMOVE	"remove"		/* remove file on error */
#define BI_ROOTOKAY	"authorize root"	/* root authenticated */
#define BI_SECURE	"authorize secure"	/* okay on non-secure line */
#define BI_SETENV	"setenv"		/* set environment variable */
#define BI_VALUE	"value"			/* set local variable */

#define AUTH_OKAY		0x01		/* user authenticated */
#define AUTH_ROOTOKAY		0x02		/* root login okay */
#define AUTH_SECURE		0x04		/* secure login */
#define AUTH_SILENT		0x08		/* silent rejection */
#define AUTH_CHALLENGE		0x10		/* a chellenge was given */

#define AUTH_ALLOW		(AUTH_OKAY | AUTH_ROOTOKAY | AUTH_SECURE)

typedef struct login_cap {
    char    *lc_class;
    char    *lc_cap;
    char    *lc_style;
} login_cap_t;

typedef struct login_time {
    u_short     lt_start;	/* Start time */
    u_short     lt_end;		/* End time */
#define LTM_NONE  0x00
#define LTM_SUN   0x01
#define LTM_MON   0x02
#define LTM_TUE   0x04
#define LTM_WED   0x08
#define LTM_THU   0x10
#define LTM_FRI   0x20
#define LTM_SAT   0x40
#define LTM_ANY   0x7F
#define LTM_WK    0x3E
#define LTM_WD    0x41
    u_char	 lt_dow;	/* Days of week */
} login_time_t;

#define LC_MAXTIMES 64

#include <sys/cdefs.h>
__BEGIN_DECLS
struct passwd;

void login_close(login_cap_t *);
login_cap_t *login_getclassbyname(const char *, const struct passwd *);
login_cap_t *login_getclass(const char *);
login_cap_t *login_getpwclass(const struct passwd *);
login_cap_t *login_getuserclass(const struct passwd *);

const char *login_getcapstr(login_cap_t *, const char *, const char *,
    const char *);
const char **login_getcaplist(login_cap_t *, const char *, const char *);
const char *login_getstyle(login_cap_t *, const char *, const char *);
rlim_t login_getcaptime(login_cap_t *, const char *, rlim_t, rlim_t);
rlim_t login_getcapnum(login_cap_t *, const char *, rlim_t, rlim_t);
rlim_t login_getcapsize(login_cap_t *, const char *, rlim_t, rlim_t);
const char *login_getpath(login_cap_t *, const char *, const char *);
int login_getcapbool(login_cap_t *, const char *, int);
const char *login_setcryptfmt(login_cap_t *, const char *, const char *);

int setclasscontext(const char *, unsigned int);
void setclasscpumask(login_cap_t *);
int setusercontext(login_cap_t *, const struct passwd *, uid_t, unsigned int);
void setclassresources(login_cap_t *);
void setclassenvironment(login_cap_t *, const struct passwd *, int);

/* Most of these functions are deprecated */
int auth_approve(login_cap_t *, const char *, const char *);
int auth_check(const char *, const char *, const char *, const char *, int *);
void auth_env(void);
char *auth_mkvalue(const char *);
int auth_response(const char *, const char *, const char *, const char *, int *,
    const char *, const char *);
void auth_rmfiles(void);
int auth_scan(int);
int auth_script(const char *, ...);
int auth_script_data(const char *, int, const char *, ...);
char *auth_valud(const char *);
int auth_setopt(const char *, const char *);
void auth_clropts(void);

void auth_checknologin(login_cap_t *);
int auth_cat(const char *);

int auth_ttyok(login_cap_t *, const char *);
int auth_hostok(login_cap_t *, const char *, char const *);
int auth_timeok(login_cap_t *, time_t);

struct tm;

login_time_t parse_lt(const char *);
int in_lt(const login_time_t *, time_t *);
int in_ltm(const login_time_t *, struct tm *, time_t *);
int in_ltms(const login_time_t *, struct tm *, time_t *);
int in_lts(const login_time_t *, time_t *);

/* helper functions */

int login_strinlist(const char **, char const *, int);
int login_str2inlist(const char **, const char *, const char *, int);
login_time_t * login_timelist(login_cap_t *, char const *, int *,
    login_time_t **);
int login_ttyok(login_cap_t *, const char *, const char *, const char *);
int login_hostok(login_cap_t *, const char *, const char *, const char *,
    const char *);

__END_DECLS

#endif /* _LOGIN_CAP_H_ */
