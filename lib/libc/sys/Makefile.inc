#	@(#)Makefile.inc	8.3 (Berkeley) 10/24/94
# $FreeBSD$

# sys sources
.PATH: ${LIBC_SRCTOP}/${LIBC_ARCH}/sys ${LIBC_SRCTOP}/sys

# Include the generated makefile containing the *complete* list
# of syscall names in MIASM.
.include "${SRCTOP}/sys/sys/syscall.mk"

# Include machine dependent definitions.
#
# MDASM names override the default syscall names in MIASM.
# NOASM will prevent the default syscall code from being generated.
# PSEUDO generates _<sys>() and __sys_<sys>() symbols, but not <sys>().
#
# While historically machine dependent, all architectures have the following
# declarations in common:
#
NOASM=	exit.o \
	getlogin.o \
	sstk.o \
	yield.o
PSEUDO=	_exit.o \
	_getlogin.o
.sinclude "${LIBC_SRCTOP}/${LIBC_ARCH}/sys/Makefile.inc"

SRCS+= clock_gettime.c gettimeofday.c __vdso_gettimeofday.c
NOASM+=  clock_gettime.o gettimeofday.o
PSEUDO+= _clock_gettime.o _gettimeofday.o

# Sources common to both syscall interfaces:
SRCS+=	\
	__error.c \
	interposing_table.c

SRCS+= getdents.c lstat.c mknod.c stat.c

SRCS+= fstat.c fstatat.c fstatfs.c getfsstat.c statfs.c
NOASM+= fstat.o fstatat.o fstatfs.o getfsstat.o statfs.o
PSEUDO+= _fstat.o _fstatat.o _fstatfs.o _getfsstat.o _statfs.o

SRCS+= getdirentries.c
NOASM+= getdirentries.o
PSEUDO+= _getdirentries.o

SRCS+= brk.c
SRCS+= pipe.c
SRCS+= vadvise.c

SRCS+=	compat-stub.c

INTERPOSED = \
	accept \
	accept4 \
	aio_suspend \
	clock_nanosleep \
	close \
	connect \
	fcntl \
	fdatasync \
	fsync \
	fork \
	kevent \
	msync \
	nanosleep \
	open \
	openat \
	poll \
	ppoll \
	pselect \
	ptrace \
	read \
	readv \
	recvfrom \
	recvmsg \
	select \
	sendmsg \
	sendto \
	setcontext \
	sigprocmask \
	sigsuspend \
	sigtimedwait \
	sigwait \
	sigwaitinfo \
	swapcontext \
	wait4 \
	wait6 \
	write \
	writev

.if ${MACHINE_CPUARCH} == "sparc64"
SRCS+=	sigaction.c
NOASM+=	sigaction.o
.else
INTERPOSED+= sigaction
.endif

SRCS+=	${INTERPOSED:S/$/.c/}
NOASM+=	${INTERPOSED:S/$/.o/}
PSEUDO+=	${INTERPOSED:C/^.*$/_&.o/}

# Add machine dependent asm sources:
SRCS+=${MDASM}

# Look though the complete list of syscalls (MIASM) for names that are
# not defined with machine dependent implementations (MDASM) and are
# not declared for no generation of default code (NOASM).  Add each
# syscall that satisfies these conditions to the ASM list.
.for _asm in ${MIASM}
.if (${MDASM:R:M${_asm:R}} == "")
.if (${NOASM:R:M${_asm:R}} == "")
ASM+=$(_asm)
.endif
.endif
.endfor

SASM=	${ASM:S/.o/.S/}

SPSEUDO= ${PSEUDO:S/.o/.S/}

SRCS+=	${SASM} ${SPSEUDO}

SYM_MAPS+=	${LIBC_SRCTOP}/sys/Symbol.map

# Generated files
CLEANFILES+=	${SASM} ${SPSEUDO}

.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386" || \
    ${MACHINE_CPUARCH} == "powerpc" || ${MACHINE_ARCH:Marmv[67]*}
NOTE_GNU_STACK='\t.section .note.GNU-stack,"",%%progbits\n'
.else
NOTE_GNU_STACK=''
.endif

${SASM}:
	printf '#include "compat.h"\n' > ${.TARGET}
	printf '#include "SYS.h"\nRSYSCALL(${.PREFIX})\n' >> ${.TARGET}
	printf  ${NOTE_GNU_STACK} >>${.TARGET}

${SPSEUDO}:
	printf '#include "compat.h"\n' > ${.TARGET}
	printf '#include "SYS.h"\nPSEUDO(${.PREFIX:S/_//})\n' \
	    >> ${.TARGET}
	printf ${NOTE_GNU_STACK} >>${.TARGET}

MAN+=	abort2.2 \
	accept.2 \
	access.2 \
	acct.2 \
	adjtime.2 \
	aio_cancel.2 \
	aio_error.2 \
	aio_fsync.2 \
	aio_mlock.2 \
	aio_read.2 \
	aio_return.2 \
	aio_suspend.2 \
	aio_waitcomplete.2 \
	aio_write.2 \
	bind.2 \
	bindat.2 \
	brk.2 \
	cap_enter.2 \
	cap_fcntls_limit.2 \
	cap_ioctls_limit.2 \
	cap_rights_limit.2 \
	chdir.2 \
	chflags.2 \
	chmod.2 \
	chown.2 \
	chroot.2 \
	clock_gettime.2 \
	close.2 \
	closefrom.2 \
	connect.2 \
	connectat.2 \
	cpuset.2 \
	cpuset_getaffinity.2 \
	cpuset_getdomain.2 \
	dup.2 \
	execve.2 \
	_exit.2 \
	extattr_get_file.2 \
	fcntl.2 \
	ffclock.2 \
	fhlink.2 \
	fhopen.2 \
	fhreadlink.2 \
	flock.2 \
	fork.2 \
	fsync.2 \
	getdirentries.2 \
	getdtablesize.2 \
	getfh.2 \
	getfsstat.2 \
	getgid.2 \
	getgroups.2 \
	getitimer.2 \
	getlogin.2 \
	getloginclass.2 \
	getpeername.2 \
	getpgrp.2 \
	getpid.2 \
	getpriority.2 \
	getrandom.2 \
	getrlimit.2 \
	getrusage.2 \
	getsid.2 \
	getsockname.2 \
	getsockopt.2 \
	gettimeofday.2 \
	getuid.2 \
	intro.2 \
	ioctl.2 \
	issetugid.2 \
	jail.2 \
	kenv.2 \
	kill.2 \
	kldfind.2 \
	kldfirstmod.2 \
	kldload.2 \
	kldnext.2 \
	kldstat.2 \
	kldsym.2 \
	kldunload.2 \
	kqueue.2 \
	ktrace.2 \
	link.2 \
	lio_listio.2 \
	listen.2 \
	lseek.2 \
	madvise.2 \
	mincore.2 \
	minherit.2 \
	mkdir.2 \
	mkfifo.2 \
	mknod.2 \
	mlock.2 \
	mlockall.2 \
	mmap.2 \
	modfind.2 \
	modnext.2 \
	modstat.2 \
	mount.2 \
	mprotect.2 \
	mq_close.2 \
	mq_getattr.2 \
	mq_notify.2 \
	mq_open.2 \
	mq_receive.2 \
	mq_send.2 \
	mq_setattr.2 \
	msgctl.2 \
	msgget.2 \
	msgrcv.2 \
	msgsnd.2 \
	msync.2 \
	munmap.2 \
	nanosleep.2 \
	nfssvc.2 \
	ntp_adjtime.2 \
	open.2 \
	pathconf.2 \
	pdfork.2 \
	pipe.2 \
	poll.2 \
	posix_fadvise.2 \
	posix_fallocate.2 \
	posix_openpt.2 \
	procctl.2 \
	profil.2 \
	pselect.2 \
	ptrace.2 \
	quotactl.2 \
	rctl_add_rule.2 \
	read.2 \
	readlink.2 \
	reboot.2 \
	recv.2 \
	rename.2 \
	revoke.2 \
	rfork.2 \
	rmdir.2 \
	rtprio.2
.if !defined(NO_P1003_1B)
MAN+=	sched_get_priority_max.2 \
	sched_setparam.2 \
	sched_setscheduler.2 \
	sched_yield.2
.endif
MAN+=	sctp_generic_recvmsg.2 \
	sctp_generic_sendmsg.2 \
	sctp_peeloff.2 \
	select.2 \
	semctl.2 \
	semget.2 \
	semop.2 \
	send.2 \
	setfib.2 \
	sendfile.2 \
	setgroups.2 \
	setpgid.2 \
	setregid.2 \
	setresuid.2 \
	setreuid.2 \
	setsid.2 \
	setuid.2 \
	shmat.2 \
	shmctl.2 \
	shmget.2 \
	shm_open.2 \
	shutdown.2 \
	sigaction.2 \
	sigaltstack.2 \
	sigpending.2 \
	sigprocmask.2 \
	sigqueue.2 \
	sigreturn.2 \
	sigstack.2 \
	sigsuspend.2 \
	sigwait.2 \
	sigwaitinfo.2 \
	socket.2 \
	socketpair.2 \
	stat.2 \
	statfs.2 \
	swapon.2 \
	symlink.2 \
	sync.2 \
	sysarch.2 \
	syscall.2 \
	thr_exit.2 \
	thr_kill.2 \
	thr_new.2 \
	thr_self.2 \
	thr_set_name.2 \
	thr_suspend.2 \
	thr_wake.2 \
	timer_create.2 \
	timer_delete.2 \
	timer_settime.2 \
	truncate.2 \
	umask.2 \
	undelete.2 \
	unlink.2 \
	utimensat.2 \
	utimes.2 \
	utrace.2 \
	uuidgen.2 \
	vfork.2 \
	wait.2 \
	write.2 \
	_umtx_op.2

MLINKS+=accept.2 accept4.2
MLINKS+=access.2 eaccess.2 \
	access.2 faccessat.2
MLINKS+=brk.2 sbrk.2
MLINKS+=cap_enter.2 cap_getmode.2
MLINKS+=cap_fcntls_limit.2 cap_fcntls_get.2
MLINKS+=cap_ioctls_limit.2 cap_ioctls_get.2
MLINKS+=chdir.2 fchdir.2
MLINKS+=chflags.2 chflagsat.2 \
	chflags.2 fchflags.2 \
	chflags.2 lchflags.2
MLINKS+=chmod.2 fchmod.2 \
	chmod.2 fchmodat.2 \
	chmod.2 lchmod.2
MLINKS+=chown.2 fchown.2 \
	chown.2 fchownat.2 \
	chown.2 lchown.2
MLINKS+=clock_gettime.2 clock_getres.2 \
	clock_gettime.2 clock_settime.2
MLINKS+=nanosleep.2 clock_nanosleep.2
MLINKS+=cpuset.2 cpuset_getid.2 \
	cpuset.2 cpuset_setid.2
MLINKS+=cpuset_getaffinity.2 cpuset_setaffinity.2
MLINKS+=cpuset_getdomain.2 cpuset_setdomain.2
MLINKS+=dup.2 dup2.2
MLINKS+=execve.2 fexecve.2
MLINKS+=extattr_get_file.2 extattr.2 \
	extattr_get_file.2 extattr_delete_fd.2 \
	extattr_get_file.2 extattr_delete_file.2 \
	extattr_get_file.2 extattr_delete_link.2 \
	extattr_get_file.2 extattr_get_fd.2 \
	extattr_get_file.2 extattr_get_link.2 \
	extattr_get_file.2 extattr_list_fd.2 \
	extattr_get_file.2 extattr_list_file.2 \
	extattr_get_file.2 extattr_list_link.2 \
	extattr_get_file.2 extattr_set_fd.2 \
	extattr_get_file.2 extattr_set_file.2 \
	extattr_get_file.2 extattr_set_link.2
MLINKS+=ffclock.2 ffclock_getcounter.2 \
	ffclock.2 ffclock_getestimate.2 \
	ffclock.2 ffclock_setestimate.2
MLINKS+=fhopen.2 fhstat.2 fhopen.2 fhstatfs.2
MLINKS+=fsync.2 fdatasync.2
MLINKS+=getdirentries.2 getdents.2
MLINKS+=getfh.2 lgetfh.2 \
	getfh.2 getfhat.2
MLINKS+=getgid.2 getegid.2
MLINKS+=getitimer.2 setitimer.2
MLINKS+=getlogin.2 getlogin_r.3
MLINKS+=getlogin.2 setlogin.2
MLINKS+=getloginclass.2 setloginclass.2
MLINKS+=getpgrp.2 getpgid.2
MLINKS+=getpid.2 getppid.2
MLINKS+=getpriority.2 setpriority.2
MLINKS+=getrlimit.2 setrlimit.2
MLINKS+=getsockopt.2 setsockopt.2
MLINKS+=gettimeofday.2 settimeofday.2
MLINKS+=getuid.2 geteuid.2
MLINKS+=intro.2 errno.2
MLINKS+=jail.2 jail_attach.2 \
	jail.2 jail_get.2 \
	jail.2 jail_remove.2 \
	jail.2 jail_set.2
MLINKS+=kldunload.2 kldunloadf.2
MLINKS+=kqueue.2 kevent.2 \
	kqueue.2 EV_SET.3
MLINKS+=link.2 linkat.2
MLINKS+=madvise.2 posix_madvise.2
MLINKS+=mkdir.2 mkdirat.2
MLINKS+=mkfifo.2 mkfifoat.2
MLINKS+=mknod.2 mknodat.2
MLINKS+=mlock.2 munlock.2
MLINKS+=mlockall.2 munlockall.2
MLINKS+=modnext.2 modfnext.2
MLINKS+=mount.2 nmount.2 \
	mount.2 unmount.2
MLINKS+=mq_receive.2 mq_timedreceive.2
MLINKS+=mq_send.2 mq_timedsend.2
MLINKS+=ntp_adjtime.2 ntp_gettime.2
MLINKS+=open.2 openat.2
MLINKS+=pathconf.2 fpathconf.2
MLINKS+=pathconf.2 lpathconf.2
MLINKS+=pdfork.2 pdgetpid.2\
	pdfork.2 pdkill.2
MLINKS+=pipe.2 pipe2.2
MLINKS+=poll.2 ppoll.2
MLINKS+=rctl_add_rule.2 rctl_get_limits.2 \
	rctl_add_rule.2 rctl_get_racct.2 \
	rctl_add_rule.2 rctl_get_rules.2 \
	rctl_add_rule.2 rctl_remove_rule.2
MLINKS+=read.2 pread.2 \
	read.2 preadv.2 \
	read.2 readv.2
MLINKS+=readlink.2 readlinkat.2
MLINKS+=recv.2 recvfrom.2 \
	recv.2 recvmsg.2
MLINKS+=rename.2 renameat.2
MLINKS+=rtprio.2 rtprio_thread.2
.if !defined(NO_P1003_1B)
MLINKS+=sched_get_priority_max.2 sched_get_priority_min.2 \
	sched_get_priority_max.2 sched_rr_get_interval.2
MLINKS+=sched_setparam.2 sched_getparam.2
MLINKS+=sched_setscheduler.2 sched_getscheduler.2
.endif
MLINKS+=select.2 FD_CLR.3 \
	select.2 FD_ISSET.3 \
	select.2 FD_SET.3 \
	select.2 FD_ZERO.3
MLINKS+=send.2 sendmsg.2 \
	send.2 sendto.2
MLINKS+=setpgid.2 setpgrp.2
MLINKS+=setresuid.2 getresgid.2 \
	setresuid.2 getresuid.2 \
	setresuid.2 setresgid.2
MLINKS+=setuid.2 setegid.2 \
	setuid.2 seteuid.2 \
	setuid.2 setgid.2
MLINKS+=shmat.2 shmdt.2
MLINKS+=shm_open.2 shm_unlink.2
MLINKS+=sigwaitinfo.2 sigtimedwait.2
MLINKS+=stat.2 fstat.2 \
	stat.2 fstatat.2 \
	stat.2 lstat.2
MLINKS+=statfs.2 fstatfs.2
MLINKS+=swapon.2 swapoff.2
MLINKS+=symlink.2 symlinkat.2
MLINKS+=syscall.2 __syscall.2
MLINKS+=timer_settime.2 timer_getoverrun.2 \
	timer_settime.2 timer_gettime.2
MLINKS+=thr_kill.2 thr_kill2.2
MLINKS+=truncate.2 ftruncate.2
MLINKS+=unlink.2 unlinkat.2
MLINKS+=utimensat.2 futimens.2
MLINKS+=utimes.2 futimes.2 \
	utimes.2 futimesat.2 \
	utimes.2 lutimes.2
MLINKS+=wait.2 wait3.2 \
	wait.2 wait4.2 \
	wait.2 waitpid.2 \
	wait.2 waitid.2 \
	wait.2 wait6.2
MLINKS+=write.2 pwrite.2 \
	write.2 pwritev.2 \
	write.2 writev.2
