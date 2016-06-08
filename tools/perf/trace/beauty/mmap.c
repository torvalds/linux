#include <sys/mman.h>

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
#ifdef PROT_SEM
	P_MMAP_PROT(SEM);
#endif
	P_MMAP_PROT(GROWSDOWN);
	P_MMAP_PROT(GROWSUP);
#undef P_MMAP_PROT

	if (prot)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", prot);

	return printed;
}

#define SCA_MMAP_PROT syscall_arg__scnprintf_mmap_prot

#ifndef MAP_STACK
# define MAP_STACK		0x20000
#endif

static size_t syscall_arg__scnprintf_mmap_flags(char *bf, size_t size,
						struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

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
	P_MMAP_FLAG(GROWSDOWN);
#ifdef MAP_HUGETLB
	P_MMAP_FLAG(HUGETLB);
#endif
	P_MMAP_FLAG(LOCKED);
	P_MMAP_FLAG(NONBLOCK);
	P_MMAP_FLAG(NORESERVE);
	P_MMAP_FLAG(POPULATE);
	P_MMAP_FLAG(STACK);
#ifdef MAP_UNINITIALIZED
	P_MMAP_FLAG(UNINITIALIZED);
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
#ifdef MREMAP_FIXED
	P_MREMAP_FLAG(FIXED);
#endif
#undef P_MREMAP_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_MREMAP_FLAGS syscall_arg__scnprintf_mremap_flags

#ifndef MADV_HWPOISON
#define MADV_HWPOISON		100
#endif

#ifndef MADV_MERGEABLE
#define MADV_MERGEABLE		 12
#endif

#ifndef MADV_UNMERGEABLE
#define MADV_UNMERGEABLE	 13
#endif

static size_t syscall_arg__scnprintf_madvise_behavior(char *bf, size_t size,
						      struct syscall_arg *arg)
{
	int behavior = arg->val;

	switch (behavior) {
#define	P_MADV_BHV(n) case MADV_##n: return scnprintf(bf, size, #n)
	P_MADV_BHV(NORMAL);
	P_MADV_BHV(RANDOM);
	P_MADV_BHV(SEQUENTIAL);
	P_MADV_BHV(WILLNEED);
	P_MADV_BHV(DONTNEED);
	P_MADV_BHV(REMOVE);
	P_MADV_BHV(DONTFORK);
	P_MADV_BHV(DOFORK);
	P_MADV_BHV(HWPOISON);
#ifdef MADV_SOFT_OFFLINE
	P_MADV_BHV(SOFT_OFFLINE);
#endif
	P_MADV_BHV(MERGEABLE);
	P_MADV_BHV(UNMERGEABLE);
#ifdef MADV_HUGEPAGE
	P_MADV_BHV(HUGEPAGE);
#endif
#ifdef MADV_NOHUGEPAGE
	P_MADV_BHV(NOHUGEPAGE);
#endif
#ifdef MADV_DONTDUMP
	P_MADV_BHV(DONTDUMP);
#endif
#ifdef MADV_DODUMP
	P_MADV_BHV(DODUMP);
#endif
#undef P_MADV_PHV
	default: break;
	}

	return scnprintf(bf, size, "%#x", behavior);
}

#define SCA_MADV_BHV syscall_arg__scnprintf_madvise_behavior
