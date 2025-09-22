/*	$OpenBSD: bsd_auth.h,v 1.11 2017/03/09 10:13:03 fcambus Exp $	*/

/*-
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Berkeley Software Design,
 *	Inc.
 * 4. The name of Berkeley Software Design, Inc.  may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $From: bsd_auth.h,v 2.3 1999/09/08 22:13:08 prb Exp $
 */

#ifndef _BSD_AUTH_H_
#define _BSD_AUTH_H_

#include <machine/_types.h>		/* for __va_list */

typedef struct auth_session_t auth_session_t;

typedef enum {
	AUTHV_ALL,
	AUTHV_CHALLENGE,
	AUTHV_CLASS,
	AUTHV_NAME,
	AUTHV_SERVICE,
	AUTHV_STYLE,
	AUTHV_INTERACTIVE
} auth_item_t;

#include <sys/cdefs.h>
__BEGIN_DECLS
struct passwd;
struct login_cap;

char	*auth_getitem(auth_session_t *, auth_item_t);
int	 auth_setitem(auth_session_t *, auth_item_t, char *);

auth_session_t *auth_open(void);
auth_session_t *auth_verify(auth_session_t *, char *, char *, ...)
		    __attribute__((__sentinel__));

auth_session_t *auth_userchallenge(char *, char *, char *, char **);
auth_session_t *auth_usercheck(char *, char *, char *, char *);

int	 auth_userresponse(auth_session_t *, char *, int);
int	 auth_userokay(char *, char *, char *, char *);
int	 auth_approval(auth_session_t *, struct login_cap *, char *, char *);

int	 auth_close(auth_session_t *);
void	 auth_clean(auth_session_t *);

char	*auth_getvalue(auth_session_t *, char *);
int	 auth_getstate(auth_session_t *);
char	*auth_challenge(auth_session_t *);
void	 auth_setenv(auth_session_t *);
void	 auth_clrenv(auth_session_t *);

void	 auth_setstate(auth_session_t *, int);
int	 auth_call(auth_session_t *, char *, ...) 
	    __attribute__((__sentinel__));

int	 auth_setdata(auth_session_t *, void *, size_t);
int	 auth_setoption(auth_session_t *, char *, char *);
int	 auth_setpwd(auth_session_t *, struct passwd *);
void	 auth_set_va_list(auth_session_t *, __va_list);

struct passwd *auth_getpwd(auth_session_t *);

quad_t	 auth_check_expire(auth_session_t *);
quad_t	 auth_check_change(auth_session_t *);

void	 auth_clroptions(auth_session_t *);
void	 auth_clroption(auth_session_t *, char *);

char	*auth_mkvalue(char *);
void	 auth_checknologin(struct login_cap *);
int	 auth_cat(char *);

__END_DECLS

#endif /* _BSD_AUTH_H_ */
