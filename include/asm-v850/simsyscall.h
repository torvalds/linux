/*
 * include/asm-v850/simsyscall.h -- `System calls' under the v850e emulator
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SIMSYSCALL_H__
#define __V850_SIMSYSCALL_H__

#define V850_SIM_SYS_exit(a...)		V850_SIM_SYSCALL_1 (1	, ##a)
#define V850_SIM_SYS_fork(a...)		V850_SIM_SYSCALL_0 (2	, ##a)
#define V850_SIM_SYS_read(a...)		V850_SIM_SYSCALL_3 (3	, ##a)
#define V850_SIM_SYS_write(a...)	V850_SIM_SYSCALL_3 (4	, ##a)
#define V850_SIM_SYS_open(a...)		V850_SIM_SYSCALL_2 (5	, ##a)
#define V850_SIM_SYS_close(a...)	V850_SIM_SYSCALL_1 (6	, ##a)
#define V850_SIM_SYS_wait4(a...)	V850_SIM_SYSCALL_4 (7	, ##a)
/* #define V850_SIM_SYS_creat(a...)	V850_SIM_SYSCALL_1 (8	, ##a) */
/* #define V850_SIM_SYS_link(a...)	V850_SIM_SYSCALL_1 (9	, ##a) */
/* #define V850_SIM_SYS_unlink(a...)	V850_SIM_SYSCALL_1 (10	, ##a) */
#define V850_SIM_SYS_execv(a...)	V850_SIM_SYSCALL_2 (11	, ##a)
/* #define V850_SIM_SYS_chdir(a...)	V850_SIM_SYSCALL_1 (12	, ##a) */
/* #define V850_SIM_SYS_mknod(a...)	V850_SIM_SYSCALL_1 (14	, ##a) */
#define V850_SIM_SYS_chmod(a...)	V850_SIM_SYSCALL_2 (15	, ##a)
#define V850_SIM_SYS_chown(a...)	V850_SIM_SYSCALL_2 (16	, ##a)
#define V850_SIM_SYS_lseek(a...)	V850_SIM_SYSCALL_3 (19	, ##a)
/* #define V850_SIM_SYS_getpid(a...)	V850_SIM_SYSCALL_1 (20	, ##a) */
/* #define V850_SIM_SYS_isatty(a...)	V850_SIM_SYSCALL_1 (21	, ##a) */
/* #define V850_SIM_SYS_fstat(a...)	V850_SIM_SYSCALL_1 (22	, ##a) */
#define V850_SIM_SYS_time(a...)		V850_SIM_SYSCALL_1 (23	, ##a)
#define V850_SIM_SYS_poll(a...)		V850_SIM_SYSCALL_3 (24	, ##a)
#define V850_SIM_SYS_stat(a...)		V850_SIM_SYSCALL_2 (38	, ##a)
#define V850_SIM_SYS_pipe(a...)		V850_SIM_SYSCALL_1 (42	, ##a)
#define V850_SIM_SYS_times(a...)	V850_SIM_SYSCALL_1 (43	, ##a)
#define V850_SIM_SYS_execve(a...)	V850_SIM_SYSCALL_3 (59	, ##a)
#define V850_SIM_SYS_gettimeofday(a...)	V850_SIM_SYSCALL_2 (116	, ##a)
/* #define V850_SIM_SYS_utime(a...)	V850_SIM_SYSCALL_2 (201	, ##a) */
/* #define V850_SIM_SYS_wait(a...)	V850_SIM_SYSCALL_1 (202	, ##a) */

#define V850_SIM_SYS_make_raw(a...)	V850_SIM_SYSCALL_1 (1024 , ##a)


#define V850_SIM_SYSCALL_0(_call)					      \
({									      \
	register int call __asm__ ("r6") = _call;			      \
	register int rval __asm__ ("r10");				      \
	__asm__ __volatile__ ("trap 31"					      \
			      : "=r" (rval)				      \
			      : "r" (call)				      \
			      : "r11", "memory");			      \
	rval;								      \
})
#define V850_SIM_SYSCALL_1(_call, _arg0)				      \
({									      \
	register int call __asm__ ("r6") = _call;			      \
	register long arg0 __asm__ ("r7") = (long)_arg0;		      \
	register int rval __asm__ ("r10");				      \
	__asm__ __volatile__ ("trap 31"					      \
			      : "=r" (rval)				      \
			      : "r" (call), "r" (arg0)			      \
			      : "r11", "memory");			      \
	rval;								      \
})
#define V850_SIM_SYSCALL_2(_call, _arg0, _arg1)				      \
({									      \
	register int call __asm__ ("r6") = _call;			      \
	register long arg0 __asm__ ("r7") = (long)_arg0;		      \
	register long arg1 __asm__ ("r8") = (long)_arg1;		      \
	register int rval __asm__ ("r10");				      \
	__asm__ __volatile__ ("trap 31"					      \
			      : "=r" (rval)				      \
			      : "r" (call), "r" (arg0), "r" (arg1)	      \
			      : "r11", "memory");			      \
	rval;								      \
})
#define V850_SIM_SYSCALL_3(_call, _arg0, _arg1, _arg2)			      \
({									      \
	register int call __asm__ ("r6") = _call;			      \
	register long arg0 __asm__ ("r7") = (long)_arg0;		      \
	register long arg1 __asm__ ("r8") = (long)_arg1;		      \
	register long arg2 __asm__ ("r9") = (long)_arg2;		      \
	register int rval __asm__ ("r10");				      \
	__asm__ __volatile__ ("trap 31"					      \
			      : "=r" (rval)				      \
			      : "r" (call), "r" (arg0), "r" (arg1), "r" (arg2)\
			      : "r11", "memory");			      \
	rval;								      \
})

#define V850_SIM_SYSCALL(call, args...) \
   V850_SIM_SYS_##call (args)

#endif /* __V850_SIMSYSCALL_H__ */
