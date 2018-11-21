// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/mman.h>

static size_t syscall_arg__scnprintf_mmap_prot(char *bf, size_t size,
					       struct syscall_arg *arg)
{
	int printed = 0, prot = arg->val;

	if (prot == PROT_NONE)
		return scnprintf(bf, size, "NONE");
#define	P_MMAP_PROT(n) \
	if (prot & PROT_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		prot &= ~PROT_##n; \
	}

	P_MMAP_PROT(EXEC);
	P_MMAP_PROT(READ);
	P_MMAP_PROT(WRITE);
	P_MMAP_PROT(SEM);
	P_MMAP_PROT(GROWSDOWN);
	P_MMAP_PROT(GROWSUP);
#undef P_MMAP_PROT

	if (prot)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", prot);

	return printed;
}

#define SCA_MMAP_PROT syscall_arg__scnprintf_mmap_prot

static size_t syscall_arg__scnprintf_mmap_flags(char *bf, size_t size,
						struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

	if (flags & MAP_ANONYMOUS)
		arg->mask |= (1 << 4) | (1 << 5); /* Mask 4th ('fd') and 5th ('offset') args, ignored */

#define	P_MMAP_FLAG(n) \
	if (flags & MAP_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~MAP_##n; \
	}

	P_MMAP_FLAG(SHARED);
	P_MMAP_FLAG(PRIVATE);
#ifdef MAP_32BIT
	P_MMAP_FLAG(32BIT);
#endif
	P_MMAP_FLAG(ANONYMOUS);
	P_MMAP_FLAG(DENYWRITE);
	P_MMAP_FLAG(EXECUTABLE);
	P_MMAP_FLAG(FILE);
	P_MMAP_FLAG(FIXED);
#ifdef MAP_FIXED_NOREPLACE
	P_MMAP_FLAG(FIXED_NOREPLACE);
#endif
	P_MMAP_FLAG(GROWSDOWN);
	P_MMAP_FLAG(HUGETLB);
	P_MMAP_FLAG(LOCKED);
	P_MMAP_FLAG(NONBLOCK);
	P_MMAP_FLAG(NORESERVE);
	P_MMAP_FLAG(POPULATE);
	P_MMAP_FLAG(STACK);
	P_MMAP_FLAG(UNINITIALIZED);
#ifdef MAP_SYNC
	P_MMAP_FLAG(SYNC);
#endif
#undef P_MMAP_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_MMAP_FLAGS syscall_arg__scnprintf_mmap_flags

static size_t syscall_arg__scnprintf_mremap_flags(char *bf, size_t size,
						  struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

#define P_MREMAP_FLAG(n) \
	if (flags & MREMAP_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
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
       static DEFINE_STRARRAY(madvise_advices);

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
