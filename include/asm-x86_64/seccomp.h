#ifndef _ASM_SECCOMP_H

#include <linux/thread_info.h>

#ifdef TIF_32BIT
#error "unexpected TIF_32BIT on x86_64"
#else
#define TIF_32BIT TIF_IA32
#endif

#include <linux/unistd.h>
#include <asm/ia32_unistd.h>

#define __NR_seccomp_read __NR_read
#define __NR_seccomp_write __NR_write
#define __NR_seccomp_exit __NR_exit
#define __NR_seccomp_sigreturn __NR_rt_sigreturn

#define __NR_seccomp_read_32 __NR_ia32_read
#define __NR_seccomp_write_32 __NR_ia32_write
#define __NR_seccomp_exit_32 __NR_ia32_exit
#define __NR_seccomp_sigreturn_32 __NR_ia32_sigreturn

#endif /* _ASM_SECCOMP_H */
