/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_MODCTL_H
#define	_SYS_MODCTL_H

/*
 * loadable module support.
 */

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct modlmisc;
struct modlinkage;

/*
 * The following structure defines the operations used by modctl
 * to load and unload modules.  Each supported loadable module type
 * requires a set of mod_ops.
 */
struct mod_ops {
	int	(*modm_install)(struct modlmisc *, struct modlinkage *);
	int	(*modm_remove)(struct modlmisc *, struct modlinkage *);
	int	(*modm_info)(void *, struct modlinkage *, int *);
};

/*
 * The defined set of mod_ops structures for each loadable module type
 * Defined in modctl.c
 */
extern struct mod_ops mod_brandops;
#if defined(__i386) || defined(__amd64)
extern struct mod_ops mod_cpuops;
#endif
extern struct mod_ops mod_cryptoops;
extern struct mod_ops mod_driverops;
extern struct mod_ops mod_execops;
extern struct mod_ops mod_fsops;
extern struct mod_ops mod_miscops;
extern struct mod_ops mod_schedops;
extern struct mod_ops mod_strmodops;
extern struct mod_ops mod_syscallops;
extern struct mod_ops mod_sockmodops;
#ifdef _SYSCALL32_IMPL
extern struct mod_ops mod_syscallops32;
#endif
extern struct mod_ops mod_dacfops;
extern struct mod_ops mod_ippops;
extern struct mod_ops mod_pcbeops;
extern struct mod_ops mod_devfsops;
extern struct mod_ops mod_kiconvops;

/*
 * Definitions for the module specific linkage structures.
 * The first two fields are the same in all of the structures.
 * The linkinfo is for informational purposes only and is returned by
 * modctl with the MODINFO cmd.
 */

/* For cryptographic providers */
struct modlcrypto {
	struct mod_ops		*crypto_modops;
	char			*crypto_linkinfo;
};

/* For misc */
struct modlmisc {
	struct mod_ops		*misc_modops;
	char			*misc_linkinfo;
};

/*
 * Revision number of loadable modules support.  This is the value
 * that must be used in the modlinkage structure.
 */
#define	MODREV_1		1

/*
 * The modlinkage structure is the structure that the module writer
 * provides to the routines to install, remove, and stat a module.
 * The ml_linkage element is an array of pointers to linkage structures.
 * For most modules there is only one linkage structure.  We allocate
 * enough space for 3 linkage structures which happens to be the most
 * we have in any sun supplied module.  For those modules with more
 * than 3 linkage structures (which is very unlikely), a modlinkage
 * structure must be kmem_alloc'd in the module wrapper to be big enough
 * for all of the linkage structures.
 */
struct modlinkage {
	int		ml_rev;		/* rev of loadable modules system */
#ifdef _LP64
	void		*ml_linkage[7];	/* more space in 64-bit OS */
#else
	void		*ml_linkage[4];	/* NULL terminated list of */
					/* linkage structures */
#endif
};

/*
 * commands.  These are the commands supported by the modctl system call.
 */
#define	MODLOAD			0
#define	MODUNLOAD		1
#define	MODINFO			2
#define	MODRESERVED		3
#define	MODSETMINIROOT		4
#define	MODADDMAJBIND		5
#define	MODGETPATH		6
#define	MODREADSYSBIND		7
#define	MODGETMAJBIND		8
#define	MODGETNAME		9
#define	MODSIZEOF_DEVID		10
#define	MODGETDEVID		11
#define	MODSIZEOF_MINORNAME	12
#define	MODGETMINORNAME		13
#define	MODGETPATHLEN		14
#define	MODEVENTS		15
#define	MODGETFBNAME		16
#define	MODREREADDACF		17
#define	MODLOADDRVCONF		18
#define	MODUNLOADDRVCONF	19
#define	MODREMMAJBIND		20
#define	MODDEVT2INSTANCE	21
#define	MODGETDEVFSPATH_LEN	22
#define	MODGETDEVFSPATH		23
#define	MODDEVID2PATHS		24
#define	MODSETDEVPOLICY		26
#define	MODGETDEVPOLICY		27
#define	MODALLOCPRIV		28
#define	MODGETDEVPOLICYBYNAME	29
#define	MODLOADMINORPERM	31
#define	MODADDMINORPERM		32
#define	MODREMMINORPERM		33
#define	MODREMDRVCLEANUP	34
#define	MODDEVEXISTS		35
#define	MODDEVREADDIR		36
#define	MODDEVNAME		37
#define	MODGETDEVFSPATH_MI_LEN	38
#define	MODGETDEVFSPATH_MI	39
#define	MODRETIRE		40
#define	MODUNRETIRE		41
#define	MODISRETIRED		42
#define	MODDEVEMPTYDIR		43
#define	MODREMDRVALIAS		44

/*
 * sub cmds for MODEVENTS
 */
#define	MODEVENTS_FLUSH				0
#define	MODEVENTS_FLUSH_DUMP			1
#define	MODEVENTS_SET_DOOR_UPCALL_FILENAME	2
#define	MODEVENTS_GETDATA			3
#define	MODEVENTS_FREEDATA			4
#define	MODEVENTS_POST_EVENT			5
#define	MODEVENTS_REGISTER_EVENT		6

/*
 * devname subcmds for MODDEVNAME
 */
#define	MODDEVNAME_LOOKUPDOOR	0
#define	MODDEVNAME_DEVFSADMNODE	1
#define	MODDEVNAME_NSMAPS	2
#define	MODDEVNAME_PROFILE	3
#define	MODDEVNAME_RECONFIG	4
#define	MODDEVNAME_SYSAVAIL	5


/*
 * Data structure passed to modconfig command in kernel to build devfs tree
 */

struct aliases {
	struct aliases *a_next;
	char *a_name;
	int a_len;
};

#define	MAXMODCONFNAME	256

struct modconfig {
	char drvname[MAXMODCONFNAME];
	char drvclass[MAXMODCONFNAME];
	int major;
	int flags;
	int num_aliases;
	struct aliases *ap;
};

#if defined(_SYSCALL32)

struct aliases32 {
	caddr32_t a_next;
	caddr32_t a_name;
	int32_t a_len;
};

struct modconfig32 {
	char drvname[MAXMODCONFNAME];
	char drvclass[MAXMODCONFNAME];
	int32_t major;
	int32_t flags;
	int32_t num_aliases;
	caddr32_t ap;
};

#endif /* _SYSCALL32 */

/* flags for modconfig */
#define	MOD_UNBIND_OVERRIDE	0x01		/* fail unbind if in use */

/*
 * Max module path length
 */
#define	MOD_MAXPATH	256

/*
 * Default search path for modules ADDITIONAL to the directory
 * where the kernel components we booted from are.
 *
 * Most often, this will be "/platform/{platform}/kernel /kernel /usr/kernel",
 * but we don't wire it down here.
 */
#define	MOD_DEFPATH	"/kernel /usr/kernel"

/*
 * Default file name extension for autoloading modules.
 */
#define	MOD_DEFEXT	""

/*
 * Parameters for modinfo
 */
#define	MODMAXNAMELEN 32		/* max module name length */
#define	MODMAXLINKINFOLEN 32		/* max link info length */

/*
 * Module specific information.
 */
struct modspecific_info {
	char	msi_linkinfo[MODMAXLINKINFOLEN]; /* name in linkage struct */
	int	msi_p0;			/* module specific information */
};

/*
 * Structure returned by modctl with MODINFO command.
 */
#define	MODMAXLINK 10			/* max linkages modinfo can handle */

struct modinfo {
	int		   mi_info;		/* Flags for info wanted */
	int		   mi_state;		/* Flags for module state */
	int		   mi_id;		/* id of this loaded module */
	int		   mi_nextid;		/* id of next module or -1 */
	caddr_t		   mi_base;		/* virtual addr of text */
	size_t		   mi_size;		/* size of module in bytes */
	int		   mi_rev;		/* loadable modules rev */
	int		   mi_loadcnt;		/* # of times loaded */
	char		   mi_name[MODMAXNAMELEN]; /* name of module */
	struct modspecific_info mi_msinfo[MODMAXLINK];
						/* mod specific info */
};


#if defined(_SYSCALL32)

#define	MODMAXNAMELEN32 32		/* max module name length */
#define	MODMAXLINKINFOLEN32 32		/* max link info length */
#define	MODMAXLINK32 10			/* max linkages modinfo can handle */

struct modspecific_info32 {
	char	msi_linkinfo[MODMAXLINKINFOLEN32]; /* name in linkage struct */
	int32_t	msi_p0;			/* module specific information */
};

struct modinfo32 {
	int32_t		   mi_info;		/* Flags for info wanted */
	int32_t		   mi_state;		/* Flags for module state */
	int32_t		   mi_id;		/* id of this loaded module */
	int32_t		   mi_nextid;		/* id of next module or -1 */
	caddr32_t	   mi_base;		/* virtual addr of text */
	uint32_t	   mi_size;		/* size of module in bytes */
	int32_t		   mi_rev;		/* loadable modules rev */
	int32_t		   mi_loadcnt;		/* # of times loaded */
	char		   mi_name[MODMAXNAMELEN32]; /* name of module */
	struct modspecific_info32 mi_msinfo[MODMAXLINK32];
						/* mod specific info */
};

#endif /* _SYSCALL32 */

/* Values for mi_info flags */
#define	MI_INFO_ONE	1
#define	MI_INFO_ALL	2
#define	MI_INFO_CNT	4
#define	MI_INFO_LINKAGE	8	/* used internally to extract modlinkage */
/*
 * MI_INFO_NOBASE indicates caller does not need mi_base. Failure to use this
 * flag may lead 32-bit apps to receive an EOVERFLOW error from modctl(MODINFO)
 * when used with a 64-bit kernel.
 */
#define	MI_INFO_NOBASE	16

/* Values for mi_state */
#define	MI_LOADED	1
#define	MI_INSTALLED	2

/*
 * Macros to vector to the appropriate module specific routine.
 */
#define	MODL_INSTALL(MODL, MODLP) \
	(*(MODL)->misc_modops->modm_install)(MODL, MODLP)
#define	MODL_REMOVE(MODL, MODLP) \
	(*(MODL)->misc_modops->modm_remove)(MODL, MODLP)
#define	MODL_INFO(MODL, MODLP, P0) \
	(*(MODL)->misc_modops->modm_info)(MODL, MODLP, P0)

/*
 * Definitions for stubs
 */
struct mod_stub_info {
	uintptr_t mods_func_adr;
	struct mod_modinfo *mods_modinfo;
	uintptr_t mods_stub_adr;
	int (*mods_errfcn)(void);
	int mods_flag;			/* flags defined below */
};

/*
 * Definitions for mods_flag.
 */
#define	MODS_WEAK	0x01		/* weak stub (not loaded if called) */
#define	MODS_NOUNLOAD	0x02		/* module not unloadable (no _fini()) */
#define	MODS_INSTALLED	0x10		/* module installed */

struct mod_modinfo {
	char *modm_module_name;
	struct modctl *mp;
	struct mod_stub_info modm_stubs[1];
};

struct modctl_list {
	struct modctl_list *modl_next;
	struct modctl *modl_modp;
};

/*
 * Structure to manage a loadable module.
 * Note: the module (mod_mp) structure's "text" and "text_size" information
 * are replicated in the modctl structure so that mod_containing_pc()
 * doesn't have to grab any locks (modctls are persistent; modules are not.)
 */
typedef struct modctl {
	struct modctl	*mod_next;	/* &modules based list */
	struct modctl	*mod_prev;
	int		mod_id;
	void		*mod_mp;
	kthread_t	*mod_inprogress_thread;
	struct mod_modinfo *mod_modinfo;
	struct modlinkage *mod_linkage;
	char		*mod_filename;
	char		*mod_modname;

	char		mod_busy;	/* inprogress_thread has locked */
	char		mod_want;	/* someone waiting for unlock */
	char		mod_prim;	/* primary module */

	int		mod_ref;	/* ref count - from dependent or stub */

	char		mod_loaded;	/* module in memory */
	char		mod_installed;	/* post _init pre _fini */
	char		mod_loadflags;
	char		mod_delay_unload;	/* deferred unload */

	struct modctl_list *mod_requisites;	/* mods this one depends on. */
	void		*__unused;	/* NOTE: reuse (same size) is OK, */
					/* deletion causes mdb.vs.core issues */
	int		mod_loadcnt;	/* number of times mod was loaded */
	int		mod_nenabled;	/* # of enabled DTrace probes in mod */
	char		*mod_text;
	size_t		mod_text_size;

	int		mod_gencount;	/* # times loaded/unloaded */
	struct modctl	*mod_requisite_loading;	/* mod circular dependency */
} modctl_t;

/*
 * mod_loadflags
 */

#define	MOD_NOAUTOUNLOAD	0x1	/* Auto mod-unloader skips this mod */
#define	MOD_NONOTIFY		0x2	/* No krtld notifications on (un)load */
#define	MOD_NOUNLOAD		0x4	/* Assume EBUSY for all _fini's */

#define	MOD_BIND_HASHSIZE	64
#define	MOD_BIND_HASHMASK	(MOD_BIND_HASHSIZE-1)

typedef int modid_t;

/*
 * global function and data declarations
 */
extern kmutex_t mod_lock;

extern char *systemfile;
extern char **syscallnames;
extern int moddebug;

/*
 * this is the head of a doubly linked list.  Only the next and prev
 * pointers are used
 */
extern modctl_t modules;

/*
 * Only the following are part of the DDI/DKI
 */
extern int	mod_install(struct modlinkage *);
extern int	mod_remove(struct modlinkage *);
extern int	mod_info(struct modlinkage *, struct modinfo *);

/*
 * bit definitions for moddebug.
 */
#define	MODDEBUG_LOADMSG	0x80000000	/* print "[un]loading..." msg */
#define	MODDEBUG_ERRMSG		0x40000000	/* print detailed error msgs */
#define	MODDEBUG_LOADMSG2	0x20000000	/* print 2nd level msgs */
#define	MODDEBUG_RETIRE		0x10000000	/* print retire msgs */
#define	MODDEBUG_BINDING	0x00040000	/* driver/alias binding */
#define	MODDEBUG_FINI_EBUSY	0x00020000	/* pretend fini returns EBUSY */
#define	MODDEBUG_NOAUL_IPP	0x00010000	/* no Autounloading ipp mods */
#define	MODDEBUG_NOAUL_DACF	0x00008000	/* no Autounloading dacf mods */
#define	MODDEBUG_KEEPTEXT	0x00004000	/* keep text after unloading */
#define	MODDEBUG_NOAUL_DRV	0x00001000	/* no Autounloading Drivers */
#define	MODDEBUG_NOAUL_EXEC	0x00000800	/* no Autounloading Execs */
#define	MODDEBUG_NOAUL_FS	0x00000400	/* no Autounloading File sys */
#define	MODDEBUG_NOAUL_MISC	0x00000200	/* no Autounloading misc */
#define	MODDEBUG_NOAUL_SCHED	0x00000100	/* no Autounloading scheds */
#define	MODDEBUG_NOAUL_STR	0x00000080	/* no Autounloading streams */
#define	MODDEBUG_NOAUL_SYS	0x00000040	/* no Autounloading syscalls */
#define	MODDEBUG_NOCTF		0x00000020	/* do not load CTF debug data */
#define	MODDEBUG_NOAUTOUNLOAD	0x00000010	/* no autounloading at all */
#define	MODDEBUG_DDI_MOD	0x00000008	/* ddi_mod{open,sym,close} */
#define	MODDEBUG_MP_MATCH	0x00000004	/* dev_minorperm */
#define	MODDEBUG_MINORPERM	0x00000002	/* minor perm modctls */
#define	MODDEBUG_USERDEBUG	0x00000001	/* bpt after init_module() */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MODCTL_H */
