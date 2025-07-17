/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/node.h>
#include <linux/mmzone.h>
#include <linux/compaction.h>
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

/* These define the values that are enums (the bits) */
#define TRACE_GFP_FLAGS_GENERAL			\
	TRACE_GFP_EM(DMA)			\
	TRACE_GFP_EM(HIGHMEM)			\
	TRACE_GFP_EM(DMA32)			\
	TRACE_GFP_EM(MOVABLE)			\
	TRACE_GFP_EM(RECLAIMABLE)		\
	TRACE_GFP_EM(HIGH)			\
	TRACE_GFP_EM(IO)			\
	TRACE_GFP_EM(FS)			\
	TRACE_GFP_EM(ZERO)			\
	TRACE_GFP_EM(DIRECT_RECLAIM)		\
	TRACE_GFP_EM(KSWAPD_RECLAIM)		\
	TRACE_GFP_EM(WRITE)			\
	TRACE_GFP_EM(NOWARN)			\
	TRACE_GFP_EM(RETRY_MAYFAIL)		\
	TRACE_GFP_EM(NOFAIL)			\
	TRACE_GFP_EM(NORETRY)			\
	TRACE_GFP_EM(MEMALLOC)			\
	TRACE_GFP_EM(COMP)			\
	TRACE_GFP_EM(NOMEMALLOC)		\
	TRACE_GFP_EM(HARDWALL)			\
	TRACE_GFP_EM(THISNODE)			\
	TRACE_GFP_EM(ACCOUNT)			\
	TRACE_GFP_EM(ZEROTAGS)

#ifdef CONFIG_KASAN_HW_TAGS
# define TRACE_GFP_FLAGS_KASAN			\
	TRACE_GFP_EM(SKIP_ZERO)			\
	TRACE_GFP_EM(SKIP_KASAN)
#else
# define TRACE_GFP_FLAGS_KASAN
#endif

#ifdef CONFIG_LOCKDEP
# define TRACE_GFP_FLAGS_LOCKDEP		\
	TRACE_GFP_EM(NOLOCKDEP)
#else
# define TRACE_GFP_FLAGS_LOCKDEP
#endif

#ifdef CONFIG_SLAB_OBJ_EXT
# define TRACE_GFP_FLAGS_SLAB			\
	TRACE_GFP_EM(NO_OBJ_EXT)
#else
# define TRACE_GFP_FLAGS_SLAB
#endif

#define TRACE_GFP_FLAGS				\
	TRACE_GFP_FLAGS_GENERAL			\
	TRACE_GFP_FLAGS_KASAN			\
	TRACE_GFP_FLAGS_LOCKDEP			\
	TRACE_GFP_FLAGS_SLAB

#undef TRACE_GFP_EM
#define TRACE_GFP_EM(a) TRACE_DEFINE_ENUM(___GFP_##a##_BIT);

TRACE_GFP_FLAGS

/* Just in case these are ever used */
TRACE_DEFINE_ENUM(___GFP_UNUSED_BIT);
TRACE_DEFINE_ENUM(___GFP_LAST_BIT);

#define gfpflag_string(flag) {(__force unsigned long)flag, #flag}

/*
 * For the values that match the bits, use the TRACE_GFP_FLAGS
 * which will allow any updates to be included automatically.
 */
#undef TRACE_GFP_EM
#define TRACE_GFP_EM(a) gfpflag_string(__GFP_##a),

#define __def_gfpflag_names			\
	gfpflag_string(GFP_TRANSHUGE),		\
	gfpflag_string(GFP_TRANSHUGE_LIGHT),	\
	gfpflag_string(GFP_HIGHUSER_MOVABLE),	\
	gfpflag_string(GFP_HIGHUSER),		\
	gfpflag_string(GFP_USER),		\
	gfpflag_string(GFP_KERNEL_ACCOUNT),	\
	gfpflag_string(GFP_KERNEL),		\
	gfpflag_string(GFP_NOFS),		\
	gfpflag_string(GFP_ATOMIC),		\
	gfpflag_string(GFP_NOIO),		\
	gfpflag_string(GFP_NOWAIT),		\
	gfpflag_string(GFP_DMA),		\
	gfpflag_string(GFP_DMA32),		\
	gfpflag_string(__GFP_RECLAIM),		\
	TRACE_GFP_FLAGS				\
	{ 0, NULL }

#define show_gfp_flags(flags)						\
	(flags) ? __print_flags(flags, "|", __def_gfpflag_names		\
	) : "none"

#ifdef CONFIG_MMU
#define IF_HAVE_PG_MLOCK(_name) ,{1UL << PG_##_name, __stringify(_name)}
#else
#define IF_HAVE_PG_MLOCK(_name)
#endif

#ifdef CONFIG_MEMORY_FAILURE
#define IF_HAVE_PG_HWPOISON(_name) ,{1UL << PG_##_name, __stringify(_name)}
#else
#define IF_HAVE_PG_HWPOISON(_name)
#endif

#if defined(CONFIG_PAGE_IDLE_FLAG) && defined(CONFIG_64BIT)
#define IF_HAVE_PG_IDLE(_name) ,{1UL << PG_##_name, __stringify(_name)}
#else
#define IF_HAVE_PG_IDLE(_name)
#endif

#ifdef CONFIG_ARCH_USES_PG_ARCH_2
#define IF_HAVE_PG_ARCH_2(_name) ,{1UL << PG_##_name, __stringify(_name)}
#else
#define IF_HAVE_PG_ARCH_2(_name)
#endif

#ifdef CONFIG_ARCH_USES_PG_ARCH_3
#define IF_HAVE_PG_ARCH_3(_name) ,{1UL << PG_##_name, __stringify(_name)}
#else
#define IF_HAVE_PG_ARCH_3(_name)
#endif

#define DEF_PAGEFLAG_NAME(_name) { 1UL <<  PG_##_name, __stringify(_name) }

#define __def_pageflag_names						\
	DEF_PAGEFLAG_NAME(locked),					\
	DEF_PAGEFLAG_NAME(waiters),					\
	DEF_PAGEFLAG_NAME(referenced),					\
	DEF_PAGEFLAG_NAME(uptodate),					\
	DEF_PAGEFLAG_NAME(dirty),					\
	DEF_PAGEFLAG_NAME(lru),						\
	DEF_PAGEFLAG_NAME(active),					\
	DEF_PAGEFLAG_NAME(workingset),					\
	DEF_PAGEFLAG_NAME(owner_priv_1),				\
	DEF_PAGEFLAG_NAME(owner_2),					\
	DEF_PAGEFLAG_NAME(arch_1),					\
	DEF_PAGEFLAG_NAME(reserved),					\
	DEF_PAGEFLAG_NAME(private),					\
	DEF_PAGEFLAG_NAME(private_2),					\
	DEF_PAGEFLAG_NAME(writeback),					\
	DEF_PAGEFLAG_NAME(head),					\
	DEF_PAGEFLAG_NAME(reclaim),					\
	DEF_PAGEFLAG_NAME(swapbacked),					\
	DEF_PAGEFLAG_NAME(unevictable),					\
	DEF_PAGEFLAG_NAME(dropbehind)					\
IF_HAVE_PG_MLOCK(mlocked)						\
IF_HAVE_PG_HWPOISON(hwpoison)						\
IF_HAVE_PG_IDLE(idle)							\
IF_HAVE_PG_IDLE(young)							\
IF_HAVE_PG_ARCH_2(arch_2)						\
IF_HAVE_PG_ARCH_3(arch_3)

#define show_page_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_pageflag_names						\
	) : "none"

#if defined(CONFIG_PPC64)
#define __VM_ARCH_SPECIFIC_1 {VM_SAO,     "sao"           }
#elif defined(CONFIG_PARISC)
#define __VM_ARCH_SPECIFIC_1 {VM_GROWSUP,	"growsup"	}
#elif !defined(CONFIG_MMU)
#define __VM_ARCH_SPECIFIC_1 {VM_MAPPED_COPY,"mappedcopy"	}
#else
#define __VM_ARCH_SPECIFIC_1 {VM_ARCH_1,	"arch_1"	}
#endif

#ifdef CONFIG_MEM_SOFT_DIRTY
#define IF_HAVE_VM_SOFTDIRTY(flag,name) {flag, name },
#else
#define IF_HAVE_VM_SOFTDIRTY(flag,name)
#endif

#ifdef CONFIG_HAVE_ARCH_USERFAULTFD_MINOR
# define IF_HAVE_UFFD_MINOR(flag, name) {flag, name},
#else
# define IF_HAVE_UFFD_MINOR(flag, name)
#endif

#if defined(CONFIG_64BIT) || defined(CONFIG_PPC32)
# define IF_HAVE_VM_DROPPABLE(flag, name) {flag, name},
#else
# define IF_HAVE_VM_DROPPABLE(flag, name)
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
IF_HAVE_UFFD_MINOR(VM_UFFD_MINOR,	"uffd_minor"	)		\
	{VM_PFNMAP,			"pfnmap"	},		\
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
	{VM_SYNC,			"sync"		},		\
	__VM_ARCH_SPECIFIC_1				,		\
	{VM_WIPEONFORK,			"wipeonfork"	},		\
	{VM_DONTDUMP,			"dontdump"	},		\
IF_HAVE_VM_SOFTDIRTY(VM_SOFTDIRTY,	"softdirty"	)		\
	{VM_MIXEDMAP,			"mixedmap"	},		\
	{VM_HUGEPAGE,			"hugepage"	},		\
	{VM_NOHUGEPAGE,			"nohugepage"	},		\
IF_HAVE_VM_DROPPABLE(VM_DROPPABLE,	"droppable"	)		\
	{VM_MERGEABLE,			"mergeable"	}		\

#define show_vma_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	__def_vmaflag_names						\
	) : "none"

#ifdef CONFIG_COMPACTION
#define COMPACTION_STATUS					\
	EM( COMPACT_SKIPPED,		"skipped")		\
	EM( COMPACT_DEFERRED,		"deferred")		\
	EM( COMPACT_CONTINUE,		"continue")		\
	EM( COMPACT_SUCCESS,		"success")		\
	EM( COMPACT_PARTIAL_SKIPPED,	"partial_skipped")	\
	EM( COMPACT_COMPLETE,		"complete")		\
	EM( COMPACT_NO_SUITABLE_PAGE,	"no_suitable_page")	\
	EM( COMPACT_NOT_SUITABLE_ZONE,	"not_suitable_zone")	\
	EMe(COMPACT_CONTENDED,		"contended")

/* High-level compaction status feedback */
#define COMPACTION_FAILED	1
#define COMPACTION_WITHDRAWN	2
#define COMPACTION_PROGRESS	3

#define compact_result_to_feedback(result)	\
({						\
	enum compact_result __result = result;	\
	(__result == COMPACT_COMPLETE) ? COMPACTION_FAILED : \
		(__result == COMPACT_SUCCESS) ? COMPACTION_PROGRESS : COMPACTION_WITHDRAWN; \
})

#define COMPACTION_FEEDBACK		\
	EM(COMPACTION_FAILED,		"failed")	\
	EM(COMPACTION_WITHDRAWN,	"withdrawn")	\
	EMe(COMPACTION_PROGRESS,	"progress")

#define COMPACTION_PRIORITY						\
	EM(COMPACT_PRIO_SYNC_FULL,	"COMPACT_PRIO_SYNC_FULL")	\
	EM(COMPACT_PRIO_SYNC_LIGHT,	"COMPACT_PRIO_SYNC_LIGHT")	\
	EMe(COMPACT_PRIO_ASYNC,		"COMPACT_PRIO_ASYNC")
#else
#define COMPACTION_STATUS
#define COMPACTION_PRIORITY
#define COMPACTION_FEEDBACK
#endif

#ifdef CONFIG_ZONE_DMA
#define IFDEF_ZONE_DMA(X) X
#else
#define IFDEF_ZONE_DMA(X)
#endif

#ifdef CONFIG_ZONE_DMA32
#define IFDEF_ZONE_DMA32(X) X
#else
#define IFDEF_ZONE_DMA32(X)
#endif

#ifdef CONFIG_HIGHMEM
#define IFDEF_ZONE_HIGHMEM(X) X
#else
#define IFDEF_ZONE_HIGHMEM(X)
#endif

#define ZONE_TYPE						\
	IFDEF_ZONE_DMA(		EM (ZONE_DMA,	 "DMA"))	\
	IFDEF_ZONE_DMA32(	EM (ZONE_DMA32,	 "DMA32"))	\
				EM (ZONE_NORMAL, "Normal")	\
	IFDEF_ZONE_HIGHMEM(	EM (ZONE_HIGHMEM,"HighMem"))	\
				EMe(ZONE_MOVABLE,"Movable")

#define LRU_NAMES		\
		EM (LRU_INACTIVE_ANON, "inactive_anon") \
		EM (LRU_ACTIVE_ANON, "active_anon") \
		EM (LRU_INACTIVE_FILE, "inactive_file") \
		EM (LRU_ACTIVE_FILE, "active_file") \
		EMe(LRU_UNEVICTABLE, "unevictable")

/*
 * First define the enums in the above macros to be exported to userspace
 * via TRACE_DEFINE_ENUM().
 */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

COMPACTION_STATUS
COMPACTION_PRIORITY
/* COMPACTION_FEEDBACK are defines not enums. Not needed here. */
ZONE_TYPE
LRU_NAMES

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}
