#	$OpenBSD: Makefile.inc,v 1.179 2024/05/18 05:20:22 guenther Exp $
#	$NetBSD: Makefile.inc,v 1.35 1995/10/16 23:49:07 jtc Exp $
#	@(#)Makefile.inc	8.1 (Berkeley) 6/17/93

# sys sources
.PATH: ${LIBCSRCDIR}/arch/${MACHINE_CPU}/sys ${LIBCSRCDIR}/sys

# modules with non-default implementations on at least one architecture:
SRCS+=	Ovfork.S brk.S ${CERROR} \
	sbrk.S sigpending.S sigprocmask.S \
	sigsuspend.S tfork_thread.S

# glue to offer userland wrappers for some syscalls
SRCS+=	posix_madvise.c pthread_sigmask.c \
	w_fork.c w_sigaction.c w_sigprocmask.c w_sigsuspend.c w_vfork.c \
	w_clock_gettime.c w_gettimeofday.c microtime.c

# glue for compat with old syscall interfaces.
SRCS+=	ptrace.c semctl.c \
	timer_create.c timer_delete.c timer_getoverrun.c timer_gettime.c \
	timer_settime.c

# stack protector helper functions
SRCS+=	stack_protector.c

# cancelation details
SRCS+=	canceled.c

# syscalls that would be normal...except for cancellation or SIGTHR
CANCEL=	accept accept4 \
	close closefrom connect \
	fcntl fsync \
	msgrcv msgsnd msync \
	nanosleep \
	open openat \
	poll ppoll pread preadv pselect pwrite pwritev \
	read readv recvfrom recvmmsg recvmsg \
	select sendmmsg sendmsg sendto \
	wait4 waitid write writev
SRCS+=	${CANCEL:%=w_%.c}

# modules with default implementations on all architectures, unless overridden
# below:
ASM=	__semctl.o __thrsigdivert.o \
	access.o acct.o adjfreq.o adjtime.o \
	bind.o chdir.o chflags.o chflagsat.o chmod.o chown.o chroot.o \
	clock_getres.o clock_settime.o \
	dup.o dup2.o dup3.o \
	execve.o \
	faccessat.o fchdir.o fchflags.o fchmod.o fchmodat.o fchown.o \
	fchownat.o fhopen.o fhstat.o fhstatfs.o \
	flock.o fpathconf.o fstat.o fstatat.o fstatfs.o \
	ftruncate.o futex.o futimens.o futimes.o \
	getentropy.o getdents.o getfh.o getfsstat.o \
	getgroups.o getitimer.o getpeername.o getpgid.o \
	getpriority.o getresgid.o getresuid.o \
	getrlimit.o getrusage.o getsid.o getsockname.o \
	getsockopt.o ioctl.o \
	kevent.o kill.o kqueue.o kqueue1.o ktrace.o lchown.o \
	link.o linkat.o listen.o lseek.o lstat.o \
	madvise.o mimmutable.o minherit.o mkdir.o mkdirat.o mkfifo.o mkfifoat.o \
	mknod.o mknodat.o mlock.o mlockall.o mmap.o mount.o mprotect.o \
	mquery.o msgctl.o msgget.o munlock.o munlockall.o munmap.o \
	nfssvc.o \
	pathconf.o pathconfat.o pipe.o pipe2.o pledge.o profil.o \
	quotactl.o \
	readlink.o readlinkat.o reboot.o \
	rename.o renameat.o revoke.o rmdir.o \
	semget.o semop.o sendsyslog.o \
	setegid.o seteuid.o setgid.o setgroups.o setitimer.o setlogin.o \
	setpgid.o setpriority.o setregid.o setresgid.o setresuid.o \
	setreuid.o setrlimit.o setrtable.o setsid.o setsockopt.o \
	settimeofday.o setuid.o shmat.o shmctl.o shmdt.o \
	shmget.o shutdown.o sigaltstack.o socket.o \
	socketpair.o stat.o statfs.o swapctl.o symlink.o symlinkat.o \
	sysarch.o sysctl.o \
	thrkill.o truncate.o \
	unlink.o unlinkat.o \
	unmount.o unveil.o utimensat.o utimes.o utrace.o

SRCS+=	${SRCS_${MACHINE_CPU}}
.for i in ${SRCS_${MACHINE_CPU}}
ASM:=	${ASM:N${i:R}.o}
.endfor

PASM=	${ASM:.o=.po}
SASM=	${ASM:.o=.so}
DASM=	${ASM:.o=.do}

# syscalls that CANNOT FAIL.  They can return whatever value they want,
# they just never want to set errno.
ASM_NOERR=__get_tcb.o __set_tcb.o __threxit.o __thrsleep.o __thrwakeup.o \
	getdtablecount.o getegid.o geteuid.o getgid.o getlogin_r.o \
	getpgrp.o getpid.o getppid.o getrtable.o getthrid.o \
	getthrname.o getuid.o \
	issetugid.o \
	sched_yield.o setthrname.o sync.o \
	umask.o 
PASM_NOERR=	${ASM_NOERR:.o=.po}
SASM_NOERR=	${ASM_NOERR:.o=.so}
DASM_NOERR=	${ASM_NOERR:.o=.do}

PSEUDO=
PPSEUDO=${PSEUDO:.o=.po}
SPSEUDO=${PSEUDO:.o=.so}
DPSEUDO=${PSEUDO:.o=.do}

# syscalls that CANNOT FAIL and whose function has one more leading underbar
# than its matching SYS_* symbol.  E.g., _exit() --> SYS_exit
PSEUDO_NOERR=	_exit.o
PPSEUDO_NOERR=${PSEUDO_NOERR:.o=.po}
SPSEUDO_NOERR=${PSEUDO_NOERR:.o=.so}
DPSEUDO_NOERR=${PSEUDO_NOERR:.o=.do}

HIDDEN= ___realpath.o ___getcwd.o fork.o sigaction.o _ptrace.o ${CANCEL:=.o} \
	clock_gettime.o gettimeofday.o ypconnect.o
PHIDDEN=${HIDDEN:.o=.po}
SHIDDEN=${HIDDEN:.o=.so}
DHIDDEN=${HIDDEN:.o=.do}

OBJS+=	${ASM} ${ASM_NOERR} ${PSEUDO} ${PSEUDO_NOERR} ${HIDDEN}

# XXX only difference is -x removes FILE symbols?
TRIM=${LD} -x -r ${.TARGET}.o -o ${.TARGET} && rm -f ${.TARGET}.o
TRIM.debug=${LD} -X -r ${.TARGET}.o -o ${.TARGET} && rm -f ${.TARGET}.o


GEN_PREFIX=\t.file "${@:R}.S"\n\#include "SYS.h"
# Pick one of these to set what ASM is generated...
GENERATE.rsyscall=\
	echo creating ${.TARGET} && \
	printf '${GEN_PREFIX}\nRSYSCALL(${.PREFIX})\n'
GENERATE.rsyscall_noerr=\
	echo creating ${.TARGET} && \
	printf '${GEN_PREFIX}\nPSEUDO_NOERROR(${.PREFIX},${.PREFIX})\n'
GENERATE.pseudo=\
	echo creating ${.TARGET} && \
	printf '${GEN_PREFIX}\nPSEUDO(${.PREFIX},${.PREFIX:S/^_//})\n'
GENERATE.pseudo_noerr=\
	echo creating ${.TARGET} && \
	printf '${GEN_PREFIX}\nPSEUDO_NOERROR(${.PREFIX},${.PREFIX:S/^_//})\n'
GENERATE.rsyscall_hidden=\
	echo creating ${.TARGET} && \
	printf '${GEN_PREFIX}\nRSYSCALL_HIDDEN(${.PREFIX:S/^_//})\n'

# ...and one of these to control how it's compiled
FINISH=	${COMPILE.S} ${CFLAGS:M-[ID]*} ${CFLAGS:M-pipe} ${AINC} -P -x assembler-with-cpp - ${DFLAGS} -MF ${.TARGET:R}.d
FINISH.po=${FINISH} -o ${.TARGET}.o -DPROF		&& ${TRIM.debug}
FINISH.so=${FINISH} -o ${.TARGET}   ${PICFLAG}
FINISH.do=${FINISH} -o ${.TARGET}.o ${DIST_CFLAGS}	&& ${TRIM}
FINISH.o =${FINISH} -o ${.TARGET}.o			&& ${TRIM}

# Use those to generate the desired syscall stubs
${PASM}: ; @${GENERATE.rsyscall} | ${FINISH.po}
${SASM}: ; @${GENERATE.rsyscall} | ${FINISH.so}
${DASM}: ; @${GENERATE.rsyscall} | ${FINISH.do}
${ASM}:  ; @${GENERATE.rsyscall} | ${FINISH.o}

${PASM_NOERR}: ; @${GENERATE.rsyscall_noerr} | ${FINISH.po}
${SASM_NOERR}: ; @${GENERATE.rsyscall_noerr} | ${FINISH.so}
${DASM_NOERR}: ; @${GENERATE.rsyscall_noerr} | ${FINISH.do}
${ASM_NOERR}:  ; @${GENERATE.rsyscall_noerr} | ${FINISH.o}

${PPSEUDO}: ; @${GENERATE.pseudo} | ${FINISH.po}
${SPSEUDO}: ; @${GENERATE.pseudo} | ${FINISH.so}
${DPSEUDO}: ; @${GENERATE.pseudo} | ${FINISH.do}
${PSEUDO}:  ; @${GENERATE.pseudo} | ${FINISH.o}

${PPSEUDO_NOERR}: ; @${GENERATE.pseudo_noerr} | ${FINISH.po}
${SPSEUDO_NOERR}: ; @${GENERATE.pseudo_noerr} | ${FINISH.so}
${DPSEUDO_NOERR}: ; @${GENERATE.pseudo_noerr} | ${FINISH.do}
${PSEUDO_NOERR}:  ; @${GENERATE.pseudo_noerr} | ${FINISH.o}

${PHIDDEN}: ; @${GENERATE.rsyscall_hidden} | ${FINISH.po}
${SHIDDEN}: ; @${GENERATE.rsyscall_hidden} | ${FINISH.so}
${DHIDDEN}: ; @${GENERATE.rsyscall_hidden} | ${FINISH.do}
${HIDDEN}:  ; @${GENERATE.rsyscall_hidden} | ${FINISH.o}


MAN+=	__get_tcb.2 __thrsigdivert.2 __thrsleep.2 _exit.2 accept.2 \
	access.2 acct.2 adjfreq.2 adjtime.2 bind.2 brk.2 chdir.2 \
	chflags.2 chmod.2 chown.2 chroot.2 clock_gettime.2 close.2 \
	closefrom.2 connect.2 dup.2 execve.2 fcntl.2 fhopen.2 flock.2 \
	fork.2 fsync.2 futex.2 getentropy.2 getdents.2 getdtablecount.2 \
	getfh.2 getfsstat.2 getgid.2 getgroups.2 getitimer.2 getlogin.2 \
	getpeername.2 getpgrp.2 getpid.2 getpriority.2 getrlimit.2 \
	getrtable.2 getrusage.2 getsid.2 getsockname.2 getsockopt.2 \
	getthrid.2 getthrname.2 gettimeofday.2 getuid.2 \
	intro.2 ioctl.2 issetugid.2 \
	kbind.2 kill.2 kqueue.2 ktrace.2 link.2 listen.2 lseek.2 madvise.2 \
	mimmutable.2 minherit.2 mkdir.2 mkfifo.2 mknod.2 mlock.2 \
	mlockall.2 mmap.2 mount.2 mprotect.2 mquery.2 msgctl.2 \
	msgget.2 msgrcv.2 msgsnd.2 msync.2 munmap.2 nanosleep.2 \
	nfssvc.2 open.2 pathconf.2 pinsyscalls.2 pipe.2 pledge.2 \
	poll.2 profil.2 ptrace.2 quotactl.2 read.2 readlink.2 reboot.2 recv.2 \
	rename.2 revoke.2 rmdir.2 sched_yield.2 select.2 semctl.2 semget.2 \
	semop.2 send.2 setgroups.2 setpgid.2 setregid.2 \
	setresuid.2 setreuid.2 setsid.2 sendsyslog.2 setuid.2 shmat.2 \
	shmctl.2 shmget.2 shutdown.2 sigaction.2 sigaltstack.2 sigpending.2 \
	sigprocmask.2 sigreturn.2 sigsuspend.2 socket.2 \
	socketpair.2 stat.2 statfs.2 swapctl.2 symlink.2 \
	sync.2 sysarch.2 sysctl.2 thrkill.2 truncate.2 \
	umask.2 unlink.2 unveil.2 utimes.2 utrace.2 vfork.2 \
	wait.2 waitid.2 write.2 \
	ypconnect.2
