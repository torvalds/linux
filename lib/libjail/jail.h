/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 James Gritton.
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
 * $FreeBSD$
 */

#ifndef _JAIL_H
#define _JAIL_H

#define	JP_RAWVALUE	0x01
#define	JP_BOOL		0x02
#define	JP_NOBOOL	0x04
#define	JP_JAILSYS	0x08

#define JAIL_ERRMSGLEN	1024

extern char jail_errmsg[];

struct jailparam {
	char		*jp_name;
	void		*jp_value;
	size_t		 jp_valuelen;
	size_t		 jp_elemlen;
	int		 jp_ctltype;
	int		 jp_structtype;
	unsigned	 jp_flags;
};

__BEGIN_DECLS
extern int jail_getid(const char *name);
extern char *jail_getname(int jid);
extern int jail_setv(int flags, ...);
extern int jail_getv(int flags, ...);
extern int jailparam_all(struct jailparam **jpp);
extern int jailparam_init(struct jailparam *jp, const char *name);
extern int jailparam_import(struct jailparam *jp, const char *value);
extern int jailparam_import_raw(struct jailparam *jp, void *value,
	       size_t valuelen);
extern int jailparam_set(struct jailparam *jp, unsigned njp, int flags);
extern int jailparam_get(struct jailparam *jp, unsigned njp, int flags);
extern char *jailparam_export(struct jailparam *jp);
extern void jailparam_free(struct jailparam *jp, unsigned njp);
__END_DECLS

#endif /* _JAIL_H  */


