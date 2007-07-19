#!/bin/sh
#
# Check if current architecture are missing any function calls compared
# to i386.
# i386 define a number of legacy system calls that are i386 specific
# and listed below so they are ignored.
#
# Usage:
# syscallchk gcc gcc-options
#

ignore_list() {
cat << EOF
#include <asm/types.h>
#include <asm/unistd.h>

/* System calls for 32-bit kernels only */
#if BITS_PER_LONG == 64
#define __IGNORE_sendfile64
#define __IGNORE_ftruncate64
#define __IGNORE_truncate64
#define __IGNORE_stat64
#define __IGNORE_lstat64
#define __IGNORE_fstat64
#define __IGNORE_fcntl64
#define __IGNORE_fadvise64_64
#define __IGNORE_fstatat64
#define __IGNORE_fstatfs64
#define __IGNORE_statfs64
#endif

/* i386-specific or historical system calls */
#define __IGNORE_break
#define __IGNORE_stty
#define __IGNORE_gtty
#define __IGNORE_ftime
#define __IGNORE_prof
#define __IGNORE_lock
#define __IGNORE_mpx
#define __IGNORE_ulimit
#define __IGNORE_profil
#define __IGNORE_ioperm
#define __IGNORE_iopl
#define __IGNORE_idle
#define __IGNORE_modify_ldt
#define __IGNORE_ugetrlimit
#define __IGNORE_mmap2
#define __IGNORE_vm86
#define __IGNORE_vm86old
#define __IGNORE_set_thread_area
#define __IGNORE_get_thread_area
#define __IGNORE_madvise1
#define __IGNORE_oldstat
#define __IGNORE_oldfstat
#define __IGNORE_oldlstat
#define __IGNORE_oldolduname
#define __IGNORE_olduname
#define __IGNORE_umount2
#define __IGNORE_umount
#define __IGNORE_waitpid
#define __IGNORE_stime
#define __IGNORE_nice
#define __IGNORE_signal
#define __IGNORE_sigaction
#define __IGNORE_sgetmask
#define __IGNORE_sigsuspend
#define __IGNORE_sigpending
#define __IGNORE_ssetmask
#define __IGNORE_readdir
#define __IGNORE_socketcall
#define __IGNORE_ipc
#define __IGNORE_sigreturn
#define __IGNORE_sigprocmask
#define __IGNORE_bdflush
#define __IGNORE__llseek
#define __IGNORE__newselect
#define __IGNORE_create_module
#define __IGNORE_delete_module
#define __IGNORE_query_module
#define __IGNORE_get_kernel_syms
/* ... including the "new" 32-bit uid syscalls */
#define __IGNORE_lchown32
#define __IGNORE_getuid32
#define __IGNORE_getgid32
#define __IGNORE_geteuid32
#define __IGNORE_getegid32
#define __IGNORE_setreuid32
#define __IGNORE_setregid32
#define __IGNORE_getgroups32
#define __IGNORE_setgroups32
#define __IGNORE_fchown32
#define __IGNORE_setresuid32
#define __IGNORE_getresuid32
#define __IGNORE_setresgid32
#define __IGNORE_getresgid32
#define __IGNORE_chown32
#define __IGNORE_setuid32
#define __IGNORE_setgid32
#define __IGNORE_setfsuid32
#define __IGNORE_setfsgid32

/* sync_file_range had a stupid ABI. Allow sync_file_range2 instead */
#ifdef __NR_sync_file_range2
#define __IGNORE_sync_file_range
#endif

/* Unmerged syscalls for AFS, STREAMS, etc. */
#define __IGNORE_afs_syscall
#define __IGNORE_getpmsg
#define __IGNORE_putpmsg
#define __IGNORE_vserver
EOF
}

syscall_list() {
sed -n -e '/^\#define/ { s/[^_]*__NR_\([^[:space:]]*\).*/\
\#if !defined \(__NR_\1\) \&\& !defined \(__IGNORE_\1\)\
\#warning syscall \1 not implemented\
\#endif/p }' $1
}

(ignore_list && syscall_list ${srctree}/include/asm-i386/unistd.h) | \
$* -E -x c - > /dev/null
