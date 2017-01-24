/*
 * The order of these masks is important. Matching masks will be seen
 * first and the left over flags will end up showing by themselves.
 *
 * For example, if we have GFP_KERNEL before GFP_USER we wil get:
 *
 *  GFP_KERNEL|GFP_HARDWALL
 *
 * Thus most bits set go first.
 */

#define __def_gfpflag_names						\
	{(unsigned long)GFP_TRANSHUGE,		"GFP_TRANSHUGE"},	\
	{(unsigned long)GFP_TRANSHUGE_LIGHT,	"GFP_TRANSHUGE_LIGHT"}, \
	{(unsigned long)GFP_HIGHUSER_MOVABLE,	"GFP_HIGHUSER_MOVABLE"},\
	{(unsigned long)GFP_HIGHUSER,		"GFP_HIGHUSER"},	\
	{(unsigned long)GFP_USER,		"GFP_USER"},		\
	{(unsigned long)GFP_TEMPORARY,		"GFP_TEMPORARY"},	\
	{(unsigned long)GFP_KERNEL_ACCOUNT,	"GFP_KERNEL_ACCOUNT"},	\
	{(unsigned long)GFP_KERNEL,		"GFP_KERNEL"},		\
	{(unsigned long)GFP_NOFS,		"GFP_NOFS"},		\
	{(unsigned long)GFP_ATOMIC,		"GFP_ATOMIC"},		\
	{(unsigned long)GFP_NOIO,		"GFP_NOIO"},		\
	{(unsigned long)GFP_NOWAIT,		"GFP_NOWAIT"},		\
	{(unsigned long)GFP_DMA,		"GFP_DMA"},		\
	{(unsigned long)__GFP_HIGHMEM,		"__GFP_HIGHMEM"},	\
	{(unsigned long)GFP_DMA32,		"GFP_DMA32"},		\
	{(unsigned long)__GFP_HIGH,		"__GFP_HIGH"},		\
	{(unsigned long)__GFP_ATOMIC,		"__GFP_ATOMIC"},	\
	{(unsigned long)__GFP_IO,		"__GFP_IO"},		\
	{(unsigned long)__GFP_FS,		"__GFP_FS"},		\
	{(unsigned long)__GFP_COLD,		"__GFP_COLD"},		\
	{(unsigned long)__GFP_NOWARN,		"__GFP_NOWARN"},	\
	{(unsigned long)__GFP_REPEAT,		"__GFP_REPEAT"},	\
	{(unsigned long)__GFP_NOFAIL,		"__GFP_NOFAIL"},	\
	{(unsigned long)__GFP_NORETRY,		"__GFP_NORETRY"},	\
	{(unsigned long)__GFP_COMP,		"__GFP_COMP"},		\
	{(unsigned long)__GFP_ZERO,		"__GFP_ZERO"},		\
	{(unsigned long)__GFP_NOMEMALLOC,	"__GFP_NOMEMALLOC"},	\
	{(unsigned long)__GFP_MEMALLOC,		"__GFP_MEMALLOC"},	\
	{(unsigned long)__GFP_HARDWALL,		"__GFP_HARDWALL"},	\
	{(unsigned long)__GFP_THISNODE,		"__GFP_THISNODE"},	\
	{(unsigned long)__GFP_RECLAIMABLE,	"__GFP_RECLAIMABLE"},	\
	{(unsigned long)__GFP_MOVABLE,		"__GFP_MOVABLE"},	\
	{(unsigned long)__GFP_ACCOUNT,		"__GFP_ACCOUNT"},	\
	{(unsigned long)__GFP_NOTRACK,		"__GFP_NOTRACK"},	\
	{(unsigned long)__GFP_WRITE,		"__GFP_WRITE"},		\
	{(unsigned long)__GFP_RECLAIM,		"__GFP_RECLAIM"},	\
	{(unsigned long)__GFP_DIRECT_RECLAIM,	"__GFP_DIRECT_RECLAIM"},\
	{(unsigned long)__GFP_KSWAPD_RECLAIM,	"__GFP_KSWAPD_RECLAIM"}\

#define show_gfp_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_gfpflag_names						\
	) : "none"

#ifdef CONFIG_MMU
#define IF_HAVE_PG_MLOCK(flag,string) ,{1UL << flag, string}
#else
#define IF_HAVE_PG_MLOCK(flag,string)
#endif

#ifdef CONFIG_ARCH_USES_PG_UNCACHED
#define IF_HAVE_PG_UNCACHED(flag,string) ,{1UL << flag, string}
#else
#define IF_HAVE_PG_UNCACHED(flag,string)
#endif

#ifdef CONFIG_MEMORY_FAILURE
#define IF_HAVE_PG_HWPOISON(flag,string) ,{1UL << flag, string}
#else
#define IF_HAVE_PG_HWPOISON(flag,string)
#endif

#if defined(CONFIG_IDLE_PAGE_TRACKING) && defined(CONFIG_64BIT)
#define IF_HAVE_PG_IDLE(flag,string) ,{1UL << flag, string}
#else
#define IF_HAVE_PG_IDLE(flag,string)
#endif

#define __def_pageflag_names						\
	{1UL << PG_locked,		"locked"	},		\
	{1UL << PG_waiters,		"waiters"	},		\
	{1UL << PG_error,		"error"		},		\
	{1UL << PG_referenced,		"referenced"	},		\
	{1UL << PG_uptodate,		"uptodate"	},		\
	{1UL << PG_dirty,		"dirty"		},		\
	{1UL << PG_lru,			"lru"		},		\
	{1UL << PG_active,		"active"	},		\
	{1UL << PG_slab,		"slab"		},		\
	{1UL << PG_owner_priv_1,	"owner_priv_1"	},		\
	{1UL << PG_arch_1,		"arch_1"	},		\
	{1UL << PG_reserved,		"reserved"	},		\
	{1UL << PG_private,		"private"	},		\
	{1UL << PG_private_2,		"private_2"	},		\
	{1UL << PG_writeback,		"writeback"	},		\
	{1UL << PG_head,		"head"		},		\
	{1UL << PG_mappedtodisk,	"mappedtodisk"	},		\
	{1UL << PG_reclaim,		"reclaim"	},		\
	{1UL << PG_swapbacked,		"swapbacked"	},		\
	{1UL << PG_unevictable,		"unevictable"	}		\
IF_HAVE_PG_MLOCK(PG_mlocked,		"mlocked"	)		\
IF_HAVE_PG_UNCACHED(PG_uncached,	"uncached"	)		\
IF_HAVE_PG_HWPOISON(PG_hwpoison,	"hwpoison"	)		\
IF_HAVE_PG_IDLE(PG_young,		"young"		)		\
IF_HAVE_PG_IDLE(PG_idle,		"idle"		)

#define show_page_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_pageflag_names						\
	) : "none"

#if defined(CONFIG_X86)
#define __VM_ARCH_SPECIFIC_1 {VM_PAT,     "pat"           }
#elif defined(CONFIG_PPC)
#define __VM_ARCH_SPECIFIC_1 {VM_SAO,     "sao"           }
#elif defined(CONFIG_PARISC) || defined(CONFIG_METAG) || defined(CONFIG_IA64)
#define __VM_ARCH_SPECIFIC_1 {VM_GROWSUP,	"growsup"	}
#elif !defined(CONFIG_MMU)
#define __VM_ARCH_SPECIFIC_1 {VM_MAPPED_COPY,"mappedcopy"	}
#else
#define __VM_ARCH_SPECIFIC_1 {VM_ARCH_1,	"arch_1"	}
#endif

#if defined(CONFIG_X86)
#define __VM_ARCH_SPECIFIC_2 {VM_MPX,		"mpx"		}
#else
#define __VM_ARCH_SPECIFIC_2 {VM_ARCH_2,	"arch_2"	}
#endif

#ifdef CONFIG_MEM_SOFT_DIRTY
#define IF_HAVE_VM_SOFTDIRTY(flag,name) {flag, name },
#else
#define IF_HAVE_VM_SOFTDIRTY(flag,name)
#endif

#define __def_vmaflag_names						\
	{VM_READ,			"read"		},		\
	{VM_WRITE,			"write"		},		\
	{VM_EXEC,			"exec"		},		\
	{VM_SHARED,			"shared"	},		\
	{VM_MAYREAD,			"mayread"	},		\
	{VM_MAYWRITE,			"maywrite"	},		\
	{VM_MAYEXEC,			"mayexec"	},		\
	{VM_MAYSHARE,			"mayshare"	},		\
	{VM_GROWSDOWN,			"growsdown"	},		\
	{VM_UFFD_MISSING,		"uffd_missing"	},		\
	{VM_PFNMAP,			"pfnmap"	},		\
	{VM_DENYWRITE,			"denywrite"	},		\
	{VM_UFFD_WP,			"uffd_wp"	},		\
	{VM_LOCKED,			"locked"	},		\
	{VM_IO,				"io"		},		\
	{VM_SEQ_READ,			"seqread"	},		\
	{VM_RAND_READ,			"randread"	},		\
	{VM_DONTCOPY,			"dontcopy"	},		\
	{VM_DONTEXPAND,			"dontexpand"	},		\
	{VM_LOCKONFAULT,		"lockonfault"	},		\
	{VM_ACCOUNT,			"account"	},		\
	{VM_NORESERVE,			"noreserve"	},		\
	{VM_HUGETLB,			"hugetlb"	},		\
	__VM_ARCH_SPECIFIC_1				,		\
	__VM_ARCH_SPECIFIC_2				,		\
	{VM_DONTDUMP,			"dontdump"	},		\
IF_HAVE_VM_SOFTDIRTY(VM_SOFTDIRTY,	"softdirty"	)		\
	{VM_MIXEDMAP,			"mixedmap"	},		\
	{VM_HUGEPAGE,			"hugepage"	},		\
	{VM_NOHUGEPAGE,			"nohugepage"	},		\
	{VM_MERGEABLE,			"mergeable"	}		\

#define show_vma_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_vmaflag_names						\
	) : "none"
