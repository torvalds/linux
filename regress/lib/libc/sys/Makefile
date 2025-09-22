#	$OpenBSD: Makefile,v 1.18 2023/10/31 08:00:33 claudio Exp $

# Copyright (c) 2019 Moritz Buhl <openbsd@moritzbuhl.de>
# Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# Each test program in PROGS may define several numbered subtests.
# In a first step compile all programs and extract their parameters.
# For each PROG define new regression subtests based on the test number.

.if defined(NUMBERS)
REGRESS_TARGETS =	${NUMBERS:C/(.*)/run-${PROG}-\1 cleanup-${PROG}-\1/}
.else
REGRESS_TARGETS =	${PROGS:S/^/run-/}
.endif

PROGS =
PROGS +=	t_access
PROGS +=	t_bind
PROGS +=	t_chroot t_clock_gettime
PROGS +=	t_connect
PROGS +=	t_dup
PROGS +=	t_fork
PROGS +=	t_fsync
PROGS +=	t_getgroups t_getitimer t_getlogin t_getpid t_getrusage
PROGS +=	t_getsid t_getsockname t_gettimeofday
PROGS +=	t_kevent
PROGS +=	t_kill
PROGS +=	t_link t_listen
PROGS +=	t_minherit
PROGS +=	t_mkdir
PROGS +=	t_mkfifo
PROGS +=	t_mknod
PROGS +=	t_mlock
PROGS +=	t_mmap
PROGS +=	t_msgctl
PROGS +=	t_msgget
PROGS +=	t_msgrcv
PROGS +=	t_msgsnd
PROGS +=	t_msync
PROGS +=	t_pipe
PROGS +=	t_pipe2
PROGS +=	t_poll
PROGS +=	t_ppoll
PROGS +=	t_ptrace
PROGS +=	t_recvmmsg
PROGS +=	t_revoke
PROGS +=	t_select
PROGS +=	t_sendmmsg
PROGS +=	t_sendrecv
PROGS +=	t_setrlimit
PROGS +=	t_setuid
PROGS +=	t_sigaction
PROGS +=	t_sigaltstack
PROGS +=	t_socketpair
PROGS +=	t_stat
#PROGS +=	t_syscall	Deleted: test of gcc1.x syscall pad
PROGS +=	t_truncate
PROGS +=	t_umask t_unlink
PROGS +=	t_vfork
PROGS +=	t_waitid
PROGS +=	t_wait_noproc
PROGS +=	t_wait_noproc_wnohang
PROGS +=	t_write

# failing tests
REGRESS_EXPECTED_FAILURES =
REGRESS_EXPECTED_FAILURES +=	run-t_mlock-4
REGRESS_EXPECTED_FAILURES +=	run-t_msgrcv-3

. for t in run-t_fork-{3,4,5}
${t}:
	# Only works with a controlling tty
	@echo DISABLED
. endfor

run-t_vfork-2:
	# SIGSTOP with vfork is masked before exec(3)/exit(3)
	# see NetBSD: kern_sig.c,v 1.345
	@echo DISABLED

. for p in ${PROGS}
SRCS_$p =		$p.c atf-c.c
. endfor

CFLAGS += -std=gnu99

LDADD_t_getpid =	-lpthread

run-t_truncate: setup-t_truncate
setup-t_truncate:
	${SUDO} touch truncate_test.root_owned
	${SUDO} chown root:wheel truncate_test.root_owned

run-t_chroot: clean-dir
run-t_ptrace: clean-dir
clean-dir:
	${SUDO} rm -rf dir

CLEANFILES =	access dummy mmap truncate_test.root_owned

.for p in ${PROGS}
run-$p: $p
	ulimit -c unlimited && \
	ntests="`./$p -n`" && \
	echo "1..$$ntests" && \
	tnumbers="`jot -ns' ' - 1 $$ntests`" && \
	${.MAKE} -C ${.CURDIR} PROG=$p NUMBERS="$$tnumbers" regress
.endfor

.if defined(NUMBERS)
CUR_USER !=		id -g

. for n in ${NUMBERS}
DESCR_$n !=		eval `./${PROG} -i $n` && echo $$DESCR
REQ_USER_$n !=		eval `./${PROG} -i $n` && echo $$REQ_USER

.  if ${REQ_USER_$n} == "root"
REGRESS_ROOT_TARGETS +=	run-${PROG}-$n
.  endif

run-${PROG}-$n:
	@echo "$n ${DESCR_$n}"
.  if ${REQ_USER_$n} == "root"
	${SUDO} ./${PROG} -r $n
.  elif ${REQ_USER_$n} == "unprivileged" && ${CUR_USER} == 0
	${SUDO} su ${BUILDUSER} -c exec ./${PROG} -r $n
.  elif ${REQ_USER_$n} == "unprivileged" || ${REQ_USER_$n} == ""
	./${PROG} -r $n
.  else
	# bad REQ_USER: ${REQ_USER_$n}
	false
.  endif

cleanup-${PROG}-$n:
	-./${PROG} -c $n

. endfor
.endif

.include <bsd.regress.mk>
