/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_COMPAT_H
#define __ASM_GENERIC_COMPAT_H

/* These types are common across all compat ABIs */
typedef u32 compat_size_t;
typedef s32 compat_ssize_t;
typedef s32 compat_clock_t;
typedef s32 compat_pid_t;
typedef u32 compat_ino_t;
typedef s32 compat_off_t;
typedef s64 compat_loff_t;
typedef s32 compat_daddr_t;
typedef s32 compat_timer_t;
typedef s32 compat_key_t;
typedef s16 compat_short_t;
typedef s32 compat_int_t;
typedef s32 compat_long_t;
typedef u16 compat_ushort_t;
typedef u32 compat_uint_t;
typedef u32 compat_ulong_t;
typedef u32 compat_uptr_t;
typedef u32 compat_caddr_t;
typedef u32 compat_aio_context_t;
typedef u32 compat_old_sigset_t;

#ifndef __compat_uid32_t
typedef u32 __compat_uid32_t;
typedef u32 __compat_gid32_t;
#endif

#ifndef compat_mode_t
typedef u32 compat_mode_t;
#endif

#ifdef CONFIG_COMPAT_FOR_U64_ALIGNMENT
typedef s64 __attribute__((aligned(4))) compat_s64;
typedef u64 __attribute__((aligned(4))) compat_u64;
#else
typedef s64 compat_s64;
typedef u64 compat_u64;
#endif

#ifndef _COMPAT_NSIG
typedef u32 compat_sigset_word;
#define _COMPAT_NSIG _NSIG
#define _COMPAT_NSIG_BPW 32
#endif

#endif
