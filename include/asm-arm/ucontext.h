#ifndef _ASMARM_UCONTEXT_H
#define _ASMARM_UCONTEXT_H

#include <asm/fpstate.h>

/*
 * struct sigcontext only has room for the basic registers, but struct
 * ucontext now has room for all registers which need to be saved and
 * restored.  Coprocessor registers are stored in uc_regspace.  Each
 * coprocessor's saved state should start with a documented 32-bit magic
 * number, followed by a 32-bit word giving the coproccesor's saved size.
 * uc_regspace may be expanded if necessary, although this takes some
 * coordination with glibc.
 */

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;
	/* Allow for uc_sigmask growth.  Glibc uses a 1024-bit sigset_t.  */
	int		  __unused[32 - (sizeof (sigset_t) / sizeof (int))];
	/* Last for extensibility.  Eight byte aligned because some
	   coprocessors require eight byte alignment.  */
 	unsigned long	  uc_regspace[128] __attribute__((__aligned__(8)));
};

#ifdef __KERNEL__

/*
 * Coprocessor save state.  The magic values and specific
 * coprocessor's layouts are part of the userspace ABI.  Each one of
 * these should be a multiple of eight bytes and aligned to eight
 * bytes, to prevent unpredictable padding in the signal frame.
 */

#ifdef CONFIG_CRUNCH
#define CRUNCH_MAGIC		0x5065cf03
#define CRUNCH_STORAGE_SIZE	(CRUNCH_SIZE + 8)

struct crunch_sigframe {
	unsigned long	magic;
	unsigned long	size;
	struct crunch_state	storage;
} __attribute__((__aligned__(8)));
#endif

#ifdef CONFIG_IWMMXT
/* iwmmxt_area is 0x98 bytes long, preceeded by 8 bytes of signature */
#define IWMMXT_MAGIC		0x12ef842a
#define IWMMXT_STORAGE_SIZE	(IWMMXT_SIZE + 8)

struct iwmmxt_sigframe {
	unsigned long	magic;
	unsigned long	size;
	struct iwmmxt_struct storage;
} __attribute__((__aligned__(8)));
#endif /* CONFIG_IWMMXT */

#ifdef CONFIG_VFP
#if __LINUX_ARM_ARCH__ < 6
/* For ARM pre-v6, we use fstmiax and fldmiax.  This adds one extra
 * word after the registers, and a word of padding at the end for
 * alignment.  */
#define VFP_MAGIC		0x56465001
#define VFP_STORAGE_SIZE	152
#else
#define VFP_MAGIC		0x56465002
#define VFP_STORAGE_SIZE	144
#endif

struct vfp_sigframe
{
	unsigned long		magic;
	unsigned long		size;
	union vfp_state		storage;
};
#endif /* CONFIG_VFP */

/*
 * Auxiliary signal frame.  This saves stuff like FP state.
 * The layout of this structure is not part of the user ABI,
 * because the config options aren't.  uc_regspace is really
 * one of these.
 */
struct aux_sigframe {
#ifdef CONFIG_CRUNCH
	struct crunch_sigframe	crunch;
#endif
#ifdef CONFIG_IWMMXT
	struct iwmmxt_sigframe	iwmmxt;
#endif
#if 0 && defined CONFIG_VFP /* Not yet saved.  */
	struct vfp_sigframe	vfp;
#endif
	/* Something that isn't a valid magic number for any coprocessor.  */
	unsigned long		end_magic;
} __attribute__((__aligned__(8)));

#endif

#endif /* !_ASMARM_UCONTEXT_H */
