#ifndef _ASM_SECCOMP_H

#include <linux/thread_info.h> /* already defines TIF_32BIT */

#ifndef TIF_32BIT
#error "unexpected TIF_32BIT on sparc64"
#endif

#include <linux/unistd.h>

#define __NR_seccomp_read __NR_read
#define __NR_seccomp_write __NR_write
#define __NR_seccomp_exit __NR_exit
#define __NR_seccomp_sigreturn __NR_rt_sigreturn

#define __NR_seccomp_read_32 __NR_read
#define __NR_seccomp_write_32 __NR_write
#define __NR_seccomp_exit_32 __NR_exit
#define __NR_seccomp_sigreturn_32 __NR_sigreturn

#endif /* _ASM_SECCOMP_H */
