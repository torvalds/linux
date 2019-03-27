/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Doug Rabson
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

#ifndef _SYS_MODULE_H_
#define _SYS_MODULE_H_

/*
 * Module metadata types
 */
#define	MDT_DEPEND	1		/* argument is a module name */
#define	MDT_MODULE	2		/* module declaration */
#define	MDT_VERSION	3		/* module version(s) */
#define	MDT_PNP_INFO	4		/* Plug and play hints record */

#define	MDT_STRUCT_VERSION	1	/* version of metadata structure */
#define	MDT_SETNAME	"modmetadata_set"

typedef enum modeventtype {
	MOD_LOAD,
	MOD_UNLOAD,
	MOD_SHUTDOWN,
	MOD_QUIESCE
} modeventtype_t;

typedef struct module *module_t;
typedef int (*modeventhand_t)(module_t, int /* modeventtype_t */, void *);

/*
 * Struct for registering modules statically via SYSINIT.
 */
typedef struct moduledata {
	const char	*name;		/* module name */
	modeventhand_t  evhand;		/* event handler */
	void		*priv;		/* extra data */
} moduledata_t;

/*
 * A module can use this to report module specific data to the user via
 * kldstat(2).
 */
typedef union modspecific {
	int	intval;
	u_int	uintval;
	long	longval;
	u_long	ulongval;
} modspecific_t;

/*
 * Module dependency declaration
 */
struct mod_depend {
	int	md_ver_minimum;
	int	md_ver_preferred;
	int	md_ver_maximum;
};

/*
 * Module version declaration
 */
struct mod_version {
	int	mv_version;
};

struct mod_metadata {
	int		md_version;	/* structure version MDTV_* */
	int		md_type;	/* type of entry MDT_* */
	const void	*md_data;	/* specific data */
	const char	*md_cval;	/* common string label */
};

struct mod_pnp_match_info 
{
	const char *descr;	/* Description of the table */
	const char *bus;	/* Name of the bus for this table */
	const void *table;	/* Pointer to pnp table */
	int entry_len;		/* Length of each entry in the table (may be */
				/*   longer than descr describes). */
	int num_entry;		/* Number of entries in the table */
};
#ifdef	_KERNEL

#include <sys/linker_set.h>

#define	MODULE_METADATA_CONCAT(uniquifier)	_mod_metadata##uniquifier
#define	MODULE_METADATA(uniquifier, type, data, cval)			\
	static struct mod_metadata MODULE_METADATA_CONCAT(uniquifier) = {	\
		MDT_STRUCT_VERSION,					\
		type,							\
		data,							\
		cval							\
	};								\
	DATA_SET(modmetadata_set, MODULE_METADATA_CONCAT(uniquifier))

#define	MODULE_DEPEND(module, mdepend, vmin, vpref, vmax)		\
	static struct mod_depend _##module##_depend_on_##mdepend	\
	    __section(".data") = {					\
		vmin,							\
		vpref,							\
		vmax							\
	};								\
	MODULE_METADATA(_md_##module##_on_##mdepend, MDT_DEPEND,	\
	    &_##module##_depend_on_##mdepend, #mdepend)

/*
 * Every kernel has a 'kernel' module with the version set to
 * __FreeBSD_version.  We embed a MODULE_DEPEND() inside every module
 * that depends on the 'kernel' module.  It uses the current value of
 * __FreeBSD_version as the minimum and preferred versions.  For the
 * maximum version it rounds the version up to the end of its branch
 * (i.e. M99999 for M.x).  This allows a module built on M.x to work
 * on M.y systems where y >= x, but fail on M.z systems where z < x.
 */
#define	MODULE_KERNEL_MAXVER	(roundup(__FreeBSD_version, 100000) - 1)

#define	DECLARE_MODULE_WITH_MAXVER(name, data, sub, order, maxver)	\
	MODULE_DEPEND(name, kernel, __FreeBSD_version,			\
	    __FreeBSD_version, maxver);					\
	MODULE_METADATA(_md_##name, MDT_MODULE, &data, __XSTRING(name));\
	SYSINIT(name##module, sub, order, module_register_init, &data);	\
	struct __hack

#ifdef KLD_TIED
#define	DECLARE_MODULE(name, data, sub, order)				\
	DECLARE_MODULE_WITH_MAXVER(name, data, sub, order, __FreeBSD_version)
#else
#define	DECLARE_MODULE(name, data, sub, order)							\
	DECLARE_MODULE_WITH_MAXVER(name, data, sub, order, MODULE_KERNEL_MAXVER)
#endif

/*
 * The module declared with DECLARE_MODULE_TIED can only be loaded
 * into the kernel with exactly the same __FreeBSD_version.
 *
 * Use it for modules that use kernel interfaces that are not stable
 * even on STABLE/X branches.
 */
#define	DECLARE_MODULE_TIED(name, data, sub, order)			\
	DECLARE_MODULE_WITH_MAXVER(name, data, sub, order, __FreeBSD_version)

#define	MODULE_VERSION_CONCAT(module, version)	_##module##_version
#define	MODULE_VERSION(module, version)					\
	static struct mod_version MODULE_VERSION_CONCAT(module, version)\
	    __section(".data") = {					\
		version							\
	};								\
	MODULE_METADATA(MODULE_VERSION_CONCAT(module, version), MDT_VERSION,\
	    &MODULE_VERSION_CONCAT(module, version), __XSTRING(module))

/**
 * Generic macros to create pnp info hints that modules may export
 * to allow external tools to parse their internal device tables
 * to make an informed guess about what driver(s) to load.
 */
#define	MODULE_PNP_INFO(d, b, unique, t, n)				\
	static const struct mod_pnp_match_info _module_pnp_##b##_##unique = {	\
		.descr = d,						\
		.bus = #b,						\
		.table = t,						\
		.entry_len = sizeof((t)[0]),				\
		.num_entry = n						\
	};								\
	MODULE_METADATA(_md_##b##_pnpinfo_##unique, MDT_PNP_INFO,	\
	    &_module_pnp_##b##_##unique, #b);
/**
 * descr is a string that describes each entry in the table. The general
 * form is the grammar (TYPE:pnp_name[/pnp_name];)*
 * where TYPE is one of the following:
 *	U8	uint8_t element
 *	V8	like U8 and 0xff means match any
 *	G16	uint16_t element, any value >= matches
 *	L16	uint16_t element, any value <= matches
 *	M16	uint16_t element, mask of which of the following fields to use.
 *	U16	uint16_t element
 *	V16	like U16 and 0xffff means match any
 *	U32	uint32_t element
 *	V32	like U32 and 0xffffffff means match any
 *	W32	Two 16-bit values with first pnp_name in LSW and second in MSW.
 *	Z	pointer to a string to match exactly
 *	D	pointer to a string to human readable description for device
 *	P	A pointer that should be ignored
 *	E	EISA PNP Identifier (in binary, but bus publishes string)
 *	T	Key for whole table. pnp_name=value. must be last, if present.
 *
 * The pnp_name "#" is reserved for other fields that should be ignored.
 * Otherwise pnp_name must match the name from the parent device's pnpinfo
 * output. The second pnp_name is used for the W32 type.
 */

extern struct sx modules_sx;

#define	MOD_XLOCK	sx_xlock(&modules_sx)
#define	MOD_SLOCK	sx_slock(&modules_sx)
#define	MOD_XUNLOCK	sx_xunlock(&modules_sx)
#define	MOD_SUNLOCK	sx_sunlock(&modules_sx)
#define	MOD_LOCK_ASSERT	sx_assert(&modules_sx, SX_LOCKED)
#define	MOD_XLOCK_ASSERT	sx_assert(&modules_sx, SX_XLOCKED)

struct linker_file;

void	module_register_init(const void *);
int	module_register(const struct moduledata *, struct linker_file *);
module_t	module_lookupbyname(const char *);
module_t	module_lookupbyid(int);
int	module_quiesce(module_t);
void	module_reference(module_t);
void	module_release(module_t);
int	module_unload(module_t);
int	module_getid(module_t);
module_t	module_getfnext(module_t);
const char *	module_getname(module_t);
void	module_setspecific(module_t, modspecific_t *);
struct linker_file *module_file(module_t);

#ifdef	MOD_DEBUG
extern int mod_debug;
#define	MOD_DEBUG_REFS	1

#define	MOD_DPF(cat, args) do {						\
	if (mod_debug & MOD_DEBUG_##cat)				\
		printf args;						\
} while (0)

#else	/* !MOD_DEBUG */

#define	MOD_DPF(cat, args)
#endif
#endif	/* _KERNEL */

#define	MAXMODNAME	32

struct module_stat {
	int		version;	/* set to sizeof(struct module_stat) */
	char		name[MAXMODNAME];
	int		refs;
	int		id;
	modspecific_t	data;
};

#ifndef _KERNEL

#include <sys/cdefs.h>

__BEGIN_DECLS
int	modnext(int _modid);
int	modfnext(int _modid);
int	modstat(int _modid, struct module_stat *_stat);
int	modfind(const char *_name);
__END_DECLS

#endif

#endif	/* !_SYS_MODULE_H_ */
