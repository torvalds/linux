/*	$NetBSD: nsswitch.h,v 1.6 1999/01/26 01:04:07 lukem Exp $	*/
/*	$FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997, 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NSSWITCH_H
#define _NSSWITCH_H	1

#include <sys/types.h>
#include <stdarg.h>

#define NSS_MODULE_INTERFACE_VERSION 1

#ifndef _PATH_NS_CONF
#define _PATH_NS_CONF	"/etc/nsswitch.conf"
#endif

/* NSS source actions */
#define	NS_ACTION_CONTINUE	0	/* try the next source */
#define	NS_ACTION_RETURN	1	/* look no further */

#define	NS_SUCCESS	(1<<0)		/* entry was found */
#define	NS_UNAVAIL	(1<<1)		/* source not responding, or corrupt */
#define	NS_NOTFOUND	(1<<2)		/* source responded 'no such entry' */
#define	NS_TRYAGAIN	(1<<3)		/* source busy, may respond to retry */
#define NS_RETURN	(1<<4)		/* stop search, e.g. for ERANGE */
#define NS_TERMINATE	(NS_SUCCESS|NS_RETURN) /* flags that end search */
#define	NS_STATUSMASK	0x000000ff	/* bitmask to get the status flags */

/*
 * currently implemented sources
 */
#define NSSRC_FILES	"files"		/* local files */
#define	NSSRC_DB	"db"		/* database */
#define	NSSRC_DNS	"dns"		/* DNS; IN for hosts, HS for others */
#define	NSSRC_NIS	"nis"		/* YP/NIS */
#define	NSSRC_COMPAT	"compat"	/* passwd,group in YP compat mode */
#define	NSSRC_CACHE	"cache"		/* nscd daemon */
#define NSSRC_FALLBACK	"__fallback"	/* internal fallback source */

/*
 * currently implemented databases
 */
#define NSDB_HOSTS		"hosts"
#define NSDB_GROUP		"group"
#define NSDB_GROUP_COMPAT	"group_compat"
#define NSDB_NETGROUP		"netgroup"
#define NSDB_NETWORKS		"networks"
#define NSDB_PASSWD		"passwd"
#define NSDB_PASSWD_COMPAT	"passwd_compat"
#define NSDB_SHELLS		"shells"
#define NSDB_SERVICES		"services"
#define NSDB_SERVICES_COMPAT	"services_compat"
#define NSDB_SSH_HOSTKEYS	"ssh_hostkeys"
#define NSDB_PROTOCOLS		"protocols"
#define NSDB_RPC		"rpc"

/*
 * suggested databases to implement
 */
#define NSDB_ALIASES		"aliases"
#define NSDB_AUTH		"auth"
#define NSDB_AUTOMOUNT		"automount"
#define NSDB_BOOTPARAMS		"bootparams"
#define NSDB_ETHERS		"ethers"
#define NSDB_EXPORTS		"exports"
#define NSDB_NETMASKS		"netmasks"
#define NSDB_PHONES		"phones"
#define NSDB_PRINTCAP		"printcap"
#define NSDB_REMOTE		"remote"
#define NSDB_SENDMAILVARS	"sendmailvars"
#define NSDB_TERMCAP		"termcap"
#define NSDB_TTYS		"ttys"

/*
 * ns_dtab `method' function signature.
 */ 
typedef int (*nss_method)(void *_retval, void *_mdata, va_list _ap);

/*
 * Macro for generating method prototypes.
 */
#define NSS_METHOD_PROTOTYPE(method) \
	int method(void *, void *, va_list)

/*
 * ns_dtab - `nsswitch dispatch table'
 * Contains an entry for each source and the appropriate function to
 * call.  ns_dtabs are used in the nsdispatch() API in order to allow
 * the application to override built-in actions.
 */
typedef struct _ns_dtab {
	const char	 *src;		/* Source this entry implements */
	nss_method	  method;	/* Method to be called */
	void		 *mdata;	/* Data passed to method */
} ns_dtab;

/*
 * macros to help build an ns_dtab[]
 */
#define NS_FILES_CB(F,C)	{ NSSRC_FILES,	F,	C },
#define NS_COMPAT_CB(F,C)	{ NSSRC_COMPAT,	F,	C },
#define NS_FALLBACK_CB(F)	{ NSSRC_FALLBACK, F,	NULL },
 
#ifdef HESIOD
#   define NS_DNS_CB(F,C)	{ NSSRC_DNS,	F,	C },
#else
#   define NS_DNS_CB(F,C)
#endif

#ifdef YP
#   define NS_NIS_CB(F,C)	{ NSSRC_NIS,	F,	C },
#else
#   define NS_NIS_CB(F,C)
#endif

/*
 * ns_src - `nsswitch source'
 * used by the nsparser routines to store a mapping between a source
 * and its dispatch control flags for a given database.
 */
typedef struct _ns_src {
	const char	*name;
	u_int32_t	 flags;
} ns_src;


/*
 * default sourcelist (if nsswitch.conf is missing, corrupt,
 * or the requested database doesn't have an entry.
 */
extern const ns_src __nsdefaultsrc[];

/*
 * ns_mtab - NSS method table
 * An NSS module provides a mapping from (database name, method name)
 * tuples to the nss_method and associated data.
 */
typedef struct _ns_mtab {
	const char	*database;
	const char	*name;
	nss_method	 method;
	void		*mdata;
} ns_mtab;

/*
 * NSS module de-registration, called at module unload.
 */
typedef void	 (*nss_module_unregister_fn)(ns_mtab *, unsigned int);

/*
 * NSS module registration, called at module load.
 */
typedef ns_mtab *(*nss_module_register_fn)(const char *, unsigned int *,
		       nss_module_unregister_fn *);

/* 
 * Many NSS interfaces follow the getXXnam, getXXid, getXXent pattern.
 * Developers are encouraged to use nss_lookup_type where approriate.
 */
enum nss_lookup_type {
	nss_lt_name = 1,
	nss_lt_id   = 2,
	nss_lt_all  = 3
};

#ifdef _NS_PRIVATE
/*
 * private data structures for back-end nsswitch implementation
 */

/*
 * ns_dbt - `nsswitch database thang'
 * for each database in /etc/nsswitch.conf there is a ns_dbt, with its
 * name and a list of ns_src's containing the source information.
 */
typedef struct _ns_dbt {
	const char	*name;		/* name of database */
	ns_src		*srclist;	/* list of sources */
	int		 srclistsize;	/* size of srclist */
} ns_dbt;

/*
 * ns_mod - NSS module
 */
typedef struct _ns_mod {
	char		*name;		/* module name */
	void		*handle;	/* handle from dlopen */
	ns_mtab		*mtab;		/* method table */
	unsigned int	 mtabsize;	/* count of entries in method table */
	nss_module_unregister_fn unregister; /* called to unload module */
} ns_mod;

#endif /* _NS_PRIVATE */


#include <sys/cdefs.h>

__BEGIN_DECLS
extern	int	nsdispatch(void *, const ns_dtab [], const char *,
			   const char *, const ns_src [], ...);

#ifdef _NS_PRIVATE
extern	void		 _nsdbtaddsrc(ns_dbt *, const ns_src *);
extern	void		 _nsdbtput(const ns_dbt *);
extern	void		 _nsyyerror(const char *);
extern	int		 _nsyylex(void);
extern	int		 _nsyyparse(void);
extern	int		 _nsyylineno;
#ifdef _NSS_DEBUG
extern	void		 _nsdbtdump(const ns_dbt *);
#endif
#endif /* _NS_PRIVATE */

__END_DECLS

#endif /* !_NSSWITCH_H */
