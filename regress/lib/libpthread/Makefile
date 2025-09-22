# $OpenBSD: Makefile,v 1.51 2019/05/16 05:24:13 otto Exp $

# disabled because it requires a buggy behavior that uthread had:
#	dup2_race

# disabled because they relu on non-standard behaviour:
#	blocked_close blocked_dup2
#	blocked_join process_kill

# disabled because it relies on non-standard behaviour and the diff to
# fix it caused panics:
#	blocked_fifo

SUBDIR+= barrier blocked_shutdown \
	 cancel cancel2 cancel_wait close close_race closefrom cwd \
	 dlopen \
	 earlysig errno execve fork \
	 group malloc_duel netdb pcap poll preemption preemption_float \
	 pthread_atfork pthread_cond_timedwait pthread_create \
	 pthread_join pthread_kill pthread_mutex \
	 pthread_once pthread_rwlock pthread_rwlock2 pthread_specific \
	 readdir restart \
	 select semaphore setjmp sigdeliver siginfo \
	 siginterrupt signal signals signodefer sigsuspend sigwait sleep \
	 socket spinlock stack stdarg stdio switch system

.if defined(REGRESS_FULL) || make(clean) || make(cleandir) || make(obj)
SUBDIR+= setsockopt
.endif

# Not available or disabled: fcntl, getaddrinfo, pause, pw, sigmask, stdfiles

install:

.PHONY: testall
testall:
	rm -f /tmp/thread-{shared,static,debug}
	make cleandir && make obj && \
	REGRESS_LOG=/tmp/thread-shared make
	(export LDFLAGS="-static ${NOPIE_LDFLAGS}"; make clean && \
	 REGRESS_LOG=/tmp/thread-static make)
	(export DEBUG=-g; make clean && \
	 REGRESS_LOG=/tmp/thread-debug make)
	@echo "\n\nTest Results:"
	@(shared=$$(grep FAIL /tmp/thread-shared) || true; \
	  if [ "$$shared" ]; then \
	     echo "** SHARED lib failures:"; echo "$$shared"; echo; \
	  else \
	     echo "No SHARED lib failures"; \
	  fi)
	@(static=$$(grep FAIL /tmp/thread-static) || true; \
	  if [ "$$static" ]; then \
	     echo "** STATIC lib failures:"; echo "$$static"; echo; \
	  else \
	     echo "No STATIC lib failures"; \
	  fi)
	@(debug=$$(grep FAIL /tmp/thread-debug) || true; \
	  if [ "$$debug" ]; then \
	     echo "** DEBUG lib failures:"; echo "$$debug"; echo; \
	  else \
	     echo "No DEBUG lib failures"; \
	  fi)

.include <bsd.own.mk>
.include <bsd.subdir.mk>
