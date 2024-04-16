// SPDX-License-Identifier: LGPL-2.1
#include <linux/log2.h>

#include "trace/beauty/generated/mmap_prot_array.c"
static DEFINE_STRARRAY(mmap_prot, "PROT_");

static size_t mmap__scnprintf_prot(unsigned long prot, char *bf, size_t size, bool show_prefix)
{
       return strarray__scnprintf_flags(&strarray__mmap_prot, bf, size, show_prefix, prot);
}

static size_t syscall_arg__scnprintf_mmap_prot(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long prot = arg->val;

	if (prot == 0)
		return scnprintf(bf, size, "%sNONE", arg->show_string_prefix ? strarray__mmap_prot.prefix : "");

	return mmap__scnprintf_prot(prot, bf, size, arg->show_string_prefix);
}

#define SCA_MMAP_PROT syscall_arg__scnprintf_mmap_prot

#include "trace/beauty/generated/mmap_flags_array.c"
static DEFINE_STRARRAY(mmap_flags, "MAP_");

static size_t mmap__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
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

#include "trace/beauty/generated/mremap_flags_array.c"
static DEFINE_STRARRAY(mremap_flags, "MREMAP_");

static size_t mremap__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
       return strarray__scnprintf_flags(&strarray__mremap_flags, bf, size, show_prefix, flags);
}

static size_t syscall_arg__scnprintf_mremap_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;

	if (!(flags & MREMAP_FIXED))
		arg->mask |=  (1 << 5); /* Mask 5th ('new_address') args, ignored */

	return mremap__scnprintf_flags(flags, bf, size, arg->show_string_prefix);
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
