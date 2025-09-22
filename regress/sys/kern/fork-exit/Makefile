# $OpenBSD: Makefile,v 1.5 2024/08/07 18:25:39 claudio Exp $

# Copyright (c) 2021 Alexander Bluhm <bluhm@openbsd.org>
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

# To hunt kernel bugs during exit, terminate processes simultaneously.
# Fork 300 children that sleep.  Kill them together as process group.
# Sleeping can optionally be done with individual memory layout by
# executing sleep(1).  Optionally each child may create threads.
# Test killing 30 processes with 30 threads each.

PROG=		fork-exit
LDADD=		-lpthread
WARNINGS=	yes

REGRESS_TARGETS +=	run-fork1-exit
run-fork1-exit: ${PROG}
	# test forking a single child
	ulimit -p 500 -n 1000; ./fork-exit

REGRESS_TARGETS +=	run-fork-exit
run-fork-exit: ${PROG}
	# fork 300 children and kill them simultaneously as process group
	ulimit -p 500 -n 1000; ./fork-exit -p 300

REGRESS_TARGETS +=	run-fork-exec-exit
run-fork-exec-exit: ${PROG}
	# fork 300 children, exec sleep programs, and kill process group
	ulimit -p 500 -n 1000; ./fork-exit -e -p 300

REGRESS_TARGETS +=	run-fork1-thread1
run-fork1-thread1: ${PROG}
	# fork a single child and create one thread
	ulimit -p 500 -n 1000; ./fork-exit -t 1

REGRESS_TARGETS +=	run-fork1-thread
run-fork1-thread: ${PROG}
	# fork a single child and create 1000 threads
	ulimit -p 500 -n 1000; ./fork-exit -t 1000

REGRESS_TARGETS +=	run-fork-thread
run-fork-thread: ${PROG}
	# fork 30 children each with 30 threads and kill process group
	ulimit -p 500 -n 1000; ./fork-exit -p 30 -t 30

REGRESS_TARGETS +=	run-fork1-heap
run-fork1-heap: ${PROG}
	# allocate 400 MB of heap memory
	ulimit -p 500 -n 1000; ./fork-exit -h 400000

REGRESS_TARGETS +=	run-fork-heap
run-fork-heap: ${PROG}
	# allocate 400 MB of heap memory in processes
	ulimit -p 500 -n 1000; ./fork-exit -p 100 -h 4000

REGRESS_TARGETS +=	run-fork1-thread1-heap
run-fork1-thread1-heap: ${PROG}
	# allocate 400 MB of heap memory in single child and one thread
	ulimit -p 500 -n 1000; ./fork-exit -t 1 -h 400000

REGRESS_TARGETS +=	run-fork-thread-heap
run-fork-thread-heap: ${PROG}
	# allocate 400 MB of heap memory in threads
	ulimit -p 500 -n 1000; ./fork-exit -p 10 -t 100 -h 400

REGRESS_TARGETS +=	run-fork1-stack
run-fork1-stack: ${PROG}
	# allocate 32 MB of stack memory
	ulimit -p 500 -n 1000; ulimit -s 32768; ./fork-exit -s 32000

REGRESS_TARGETS +=	run-fork-stack
run-fork-stack: ${PROG}
	# allocate 400 MB of stack memory in processes
	ulimit -p 500 -n 1000; ulimit -s 32768; ./fork-exit -p 100 -s 4000

REGRESS_TARGETS +=	run-fork1-thread1-stack
run-fork1-thread1-stack: ${PROG}
	# allocate 400 MB of stack memory in single child and one thread
	ulimit -p 500 -n 1000; ./fork-exit -t 1 -s 400000

REGRESS_TARGETS +=	run-fork-thread-stack
run-fork-thread-stack: ${PROG}
	# allocate 400 MB of stack memory in threads
	ulimit -p 500 -n 1000; ./fork-exit -p 10 -t 100 -s 400

REGRESS_CLEANUP =	cleanup
cleanup:
	# check that all processes have been terminated and waited for
	! pkill -u `id -u` fork-exit

.include <bsd.regress.mk>
