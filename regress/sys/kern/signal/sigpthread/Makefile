# $OpenBSD: Makefile,v 1.2 2020/12/17 00:51:12 bluhm Exp $

PROG =		sigpthread
WARNINGS =	yes
LDADD =		-lpthread
DPADD =		${LIBPTHREAD}
CLEANFILES +=	out

# first test signal delivery while they are blocked

.for t in 0 1 2

REGRESS_TARGETS +=	run-block-thread-3-unblock-$t
run-block-thread-3-unblock-$t:
	# block signal
	# run 3 threads
	# kill process
	# suspend threads until signaled
	# unblock thread $t
	# handle signal
	./sigpthread -b -t 3 -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-main-unblock-$t
run-block-thread-3-sleep-main-unblock-$t:
	# block signal
	# run 3 threads
	# suspend threads until signaled
	# sleep in main thread, signal should be received while suspended
	# kill process
	# unblock thread $t
	# handle signal
	./sigpthread -b -s -t 3 -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-unblock-$t-sleep-thread
run-block-thread-3-sleep-thread-unblock-$t:
	# block signal
	# run 3 threads
	# kill process
	# sleep in threads, signal should be pending when suspending
	# suspend threads until signaled
	# unblock thread $t
	# handle signal
	./sigpthread -b -S -t 3 -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-unblock-unblock-$t
run-block-thread-3-sleep-unblock-unblock-$t:
	# block signal
	# run 3 threads
	# kill process
	# suspend threads until signaled
	# sleep in thread $t, others should be exited when unblocking
	# unblock thread $t
	# handle signal
	./sigpthread -b -t 3 -U -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t-unblock-$t
run-block-thread-3-kill-$t-unblock-$t:
	# block signal
	# run 3 threads
	# kill thread $t
	# suspend threads until signaled
	# unblock thread $t
	# handle signal
	./sigpthread -b -k $t -t 3 -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-main-kill-$t-unblock-$t
run-block-thread-3-sleep-main-kill-$t-unblock-$t:
	# block signal
	# run 3 threads
	# suspend threads until signaled
	# sleep in main thread, signal should be received while suspended
	# kill thread $t
	# unblock thread $t
	# handle signal
	./sigpthread -b -k $t -s -t 3 -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t-sleep-thread-unblock-$t
run-block-thread-3-kill-$t-sleep-thread-unblock-$t:
	# block signal
	# run 3 threads
	# kill thread $t
	# sleep in threads, signal should be pending when suspending
	# suspend threads until signaled
	# unblock thread $t
	# handle signal
	./sigpthread -b -k $t -S -t 3 -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t-unblock-$t-sleep-unblock
run-block-thread-3-kill-$t-unblock-$t-sleep-unblock:
	# block signal
	# run 3 threads
	# kill thread $t
	# suspend threads until signaled
	# sleep in thread $t, others should be exited when unblocking
	# unblock thread $t
	# handle signal
	./sigpthread -b -k $t -t 3 -U -u $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t
run-block-thread-3-kill-$t:
	# block signal
	# run 3 threads
	# kill thread $t
	# suspend threads until signaled
	# unblock all threads
	# handle signal
	./sigpthread -b -k $t -t 3 >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-main-kill-$t
run-block-thread-3-sleep-main-kill-$t:
	# block signal
	# run 3 threads
	# suspend threads until signaled
	# sleep in main thread, signal should be received while suspended
	# kill thread $t
	# unblock all threads
	# handle signal
	./sigpthread -b -k $t -s -t 3 >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t-sleep-thread
run-block-thread-3-kill-$t-sleep-thread:
	# block signal
	# run 3 threads
	# kill thread $t
	# sleep in threads, signal should be pending when suspending
	# suspend threads until signaled
	# unblock all threads
	# handle signal
	./sigpthread -b -k $t -S -t 3 >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t-sleep-unblock
run-block-thread-3-kill-$t-sleep-unblock:
	# block signal
	# run 3 threads
	# kill thread $t
	# suspend threads until signaled
	# sleep in all threads
	# unblock all threads
	# handle signal
	./sigpthread -b -k $t -t 3 -U >out
	grep 'signal $t' out
	test `wc -l <out` = 1

.endfor

REGRESS_TARGETS +=	run-block-thread-3
run-block-thread-3:
	# block signal
	# run 3 threads
	# kill process
	# suspend threads until signaled
	# unblock all threads
	# handle signal
	./sigpthread -b -t 3 >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-main
run-block-thread-3-sleep-main:
	# block signal
	# run 3 threads
	# suspend threads until signaled
	# sleep in main thread, signal should be received while suspended
	# kill process
	# unblock all threads
	# handle signal
	./sigpthread -b -s -t 3 >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-thread
run-block-thread-3-sleep-thread:
	# block signal
	# run 3 threads
	# kill process
	# sleep in threads, signal should be pending when suspending
	# suspend threads until signaled
	# unblock all threads
	# handle signal
	./sigpthread -b -S -t 3 >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-unblock
run-block-thread-3-sleep-unblock:
	# block signal
	# run 3 threads
	# kill process
	# suspend threads until signaled
	# sleep in all threads
	# unblock all threads
	# handle signal
	./sigpthread -b -t 3 -U >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

# check what happens if signals are not blocked but delivered immediately

.for t in 0 1 2

REGRESS_TARGETS +=	run-thread-3-kill-$t
run-thread-3-kill-$t:
	# run 3 threads
	# kill thread $t
	# handle signal
	# suspend threads until signaled
	./sigpthread -k $t -t 3 >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-thread-3-sleep-main-kill-$t
run-thread-3-sleep-main-kill-$t:
	# run 3 threads
	# suspend threads until signaled
	# sleep in main thread, signal should be received while suspended
	# kill thread $t
	# handle signal
	./sigpthread -k $t -s -t 3 >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-thread-3-kill-$t-sleep-thread
run-thread-3-kill-$t-sleep-thread:
	# run 3 threads
	# kill thread $t
	# sleep in threads, signal should be received while sleeping
	# handle signal
	# suspend threads until signaled
	./sigpthread -k $t -S -t 3 >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-thread-3-kill-$t-sleep-unblock
run-thread-3-kill-$t-sleep-unblock:
	# run 3 threads
	# kill thread $t
	# handle signal
	# suspend threads until signaled
	# sleep in all threads
	./sigpthread -k $t -t 3 -U >out
	grep 'signal $t' out
	test `wc -l <out` = 1

.endfor

REGRESS_TARGETS +=	run-thread-3
run-thread-3:
	# run 3 threads
	# kill process
	# handle signal
	# suspend threads until signaled
	./sigpthread -t 3 >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-thread-3-sleep-main
run-thread-3-sleep-main:
	# block signal
	# run 3 threads
	# suspend threads until signaled
	# sleep in main thread, signal should be received while suspended
	# kill process
	# handle signal
	./sigpthread -s -t 3 >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-thread-3-sleep-thread
run-thread-3-sleep-thread:
	# run 3 threads
	# kill process
	# sleep in threads, signal should be received while sleeping
	# handle signal
	# suspend threads until signaled
	./sigpthread -S -t 3 >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-thread-3-sleep-unblock
run-thread-3-sleep-unblock:
	# run 3 threads
	# kill process
	# handle signal
	# suspend threads until signaled
	# sleep in all threads
	./sigpthread -t 3 -U >out
	grep 'signal [0-2]' out
	test `wc -l <out` = 1

# signals are blocked and received by sigwait

.for t in 0 1 2

REGRESS_TARGETS +=	run-block-thread-3-waiter-$t
run-block-thread-3-waiter-$t:
	# block signal
	# run 3 threads
	# kill process
	# wait for signal in thread $t
	# suspend threads until signaled
	./sigpthread -b -t 3 -w $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-main-waiter-$t
run-block-thread-3-sleep-main-waiter-$t:
	# block signal
	# run 3 threads
	# wait for signal in thread $t
	# suspend threads until signaled
	# sleep in main thread, signal should be received while waiting
	# kill process
	./sigpthread -b -s -t 3 -w $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-waiter-$t-sleep-thread
run-block-thread-3-sleep-thread-waiter-$t:
	# block signal
	# run 3 threads
	# kill process
	# sleep in threads, signal should be pending when waiting
	# wait for signal in thread $t
	# suspend threads until signaled
	./sigpthread -b -S -t 3 -w $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t-waiter-$t
run-block-thread-3-kill-$t-waiter-$t:
	# block signal
	# run 3 threads
	# kill thread $t
	# wait for signal in thread $t
	# suspend threads until signaled
	./sigpthread -b -k $t -t 3 -w $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-sleep-main-kill-$t-waiter-$t
run-block-thread-3-sleep-main-kill-$t-waiter-$t:
	# block signal
	# run 3 threads
	# wait for signal in thread $t
	# suspend threads until signaled
	# sleep in main thread, signal should be received while waiting
	# kill thread $t
	./sigpthread -b -k $t -s -t 3 -w $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-3-kill-$t-sleep-thread-waiter-$t
run-block-thread-3-kill-$t-sleep-thread-waiter-$t:
	# block signal
	# run 3 threads
	# kill thread $t
	# sleep in threads, signal should be pending when waiting
	# wait for signal in thread $t
	# suspend threads until signaled
	./sigpthread -b -k $t -S -t 3 -w $t >out
	grep 'signal $t' out
	test `wc -l <out` = 1

.endfor

# simple tests with much more threads

REGRESS_TARGETS +=	run-block-thread-100-unblock-23
run-block-thread-100-unblock-23:
	# block signal
	# run 100 threads
	# kill process
	# suspend threads until signaled
	# unblock thread 23
	# handle signal
	./sigpthread -b -t 100 -u 23 >out
	grep 'signal 23' out
	test `wc -l <out` = 1

REGRESS_TARGETS +=	run-block-thread-100-waiter-42
run-block-thread-100-waiter-42:
	# block signal
	# run 100 threads
	# kill process
	# wait for signal in thread 42
	# suspend threads until signaled
	./sigpthread -b -t 100 -w 42 >out
	grep 'signal 42' out
	test `wc -l <out` = 1

${REGRESS_TARGETS}: ${PROG}

.include <bsd.regress.mk>
