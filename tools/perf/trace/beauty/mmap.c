// SPDX-License-Identifier: LGPL-2.1
#include <uapi/linux/mman.h>
#include <linux/log2.h>

static size_t syscall_arg__scnprintf_mmap_prot(char *bf, size_t size,
					       struct syscall_arg *arg)
{
	const char *prot_prefix = "PROT_";
	int printed = 0, prot = arg->val;
	bool show_prefix = arg->show_string_prefix;

	if (prot == PROT_NONE)
		return scnprintf(bf, size, "%sNONE", show_prefix ? prot_prefix : "");
#define	P_MMAP_PROT(n) \
	if (prot & PROT_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s%s", printed ? "|" : "", show_prefix ? prot_prefix :"", #n); \
		prot &= ~PROT_##n; \
	}

	P_MMAP_PROT(READ);
	P_MMAP_PROT(WRITE);
	P_MMAP_PROT(EXEC);
	P_MMAP_PROT(SEM);
	P_MMAP_PROT(GROWSDOWN);
	P_MMAP_PROT(GROWSUP);
#undef P_MMAP_PROT

	if (prot)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", prot);

	return printed;
}

#define SCA_MMAP_PROT syscall_arg__scnprintf_mmap_prot

static size_t mmap__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/mmap_flags_array.c"
       static DEFINE_STRARRAY(mmap_flags, "MAP_");

       return strarray__scnprintf_flags(&strarray__mmap_flags, bf, size, show_prefix, flags);
}

static size_t syscall_arg__scnprintf_mmap_flags(char *bf, size_t size,
						struct syscall_arg *arg)
{
	unsigned long flags = arg->val;

	if (flags & MAP_ANONYMOUS)
		arg->mask |= (1 << 4) | (1 << 5); /* Mask 4th ('fd') and 5th ('offset') args, ignored */

	return mmap__scnprintf_flags(flags, bf, size, arg->show_string_prefix);
}

#define SCA_MMAP_FLAGS syscall_arg__scnprintf_mmap_flags

static size_t syscall_arg__scnprintf_mremap_flags(char *bf, size_t size,
						  struct syscall_arg *arg)
{
	const char *flags_prefix = "MREMAP_";
	bool show_prefix = arg->show_string_prefix;
	int printed = 0, flags = arg->val;

#define P_MREMAP_FLAG(n) \
	if (flags & MREMAP_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s%s", printed ? "|" : "", show_prefix ? flags_prefix : "", #n); \
		flags &= ~MREMAP_##n; \
	}

	P_MREMAP_FLAG(MAYMOVE);
	P_MREMAP_FLAG(FIXED);
#undef P_MREMAP_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_MREMAP_FLAGS syscall_arg__scnprintf_mremap_flags

static size_t madvise__scnprintf_behavior(int behavior, char *bf, size_t size)
{
#include "trace/beauty/generated/madvise_behavior_array.c"
       static DEFINE_STRARRAY(madvise_advices, "MADV_");

       if (behavior < strarray__madvise_advices.nr_entries && strarray__madvise_advices.entries[behavior] != NULL)
               return scnprintf(bf, size, "MADV_%s", strarray__madvise_advices.entries[behavior]);

       return scnprintf(bf, size, "%#", behavior);
}

static size_t syscall_arg__scnprintf_madvise_behavior(char *bf, size_t size,
						      struct syscall_arg *arg)
{
	return madvise__scnprintf_behavior(arg->val, bf, size);
}

#define SCA_MADV_BHV syscall_arg__scnprintf_madvise_behavior
