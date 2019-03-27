#
# $FreeBSD$
#
# Makefile for building userland tests
# this is written in a form compatible with gmake

SCHED_SRCS = test_dn_sched.c
SCHED_SRCS += dn_sched_fifo.c
SCHED_SRCS += dn_sched_prio.c
SCHED_SRCS += dn_sched_qfq.c
SCHED_SRCS += dn_sched_rr.c
SCHED_SRCS += dn_sched_wf2q.c
SCHED_SRCS += dn_heap.c
SCHED_SRCS += main.c

SCHED_OBJS=$(SCHED_SRCS:.c=.o)

HEAP_SRCS = dn_heap.c test_dn_heap.c
HEAP_OBJS=$(HEAP_SRCS:.c=.o)

VPATH=	.:..

CFLAGS = -I.. -I. -Wall -Werror -O3 -Wextra
TARGETS= test_sched # no test_heap by default

all:	$(TARGETS)

test_heap : $(HEAP_OBJS)
	$(CC) -o $@ $(HEAP_OBJS)

test_sched : $(SCHED_OBJS)
	$(CC) -o $@ $(SCHED_OBJS)

$(SCHED_OBJS): dn_test.h
main.o: mylist.h

clean:
	- rm *.o $(TARGETS) *.core

ALLSRCS = $(SCHED_SRCS) dn_test.h mylist.h \
	dn_sched.h dn_heap.h ip_dn_private.h Makefile
TMPBASE = /tmp/testXYZ
TMPDIR = $(TMPBASE)/test

tgz:
	-rm -rf $(TMPDIR)
	mkdir -p $(TMPDIR)
	-cp -p $(ALLSRCS) $(TMPDIR)
	-(cd ..; cp -p $(ALLSRCS) $(TMPDIR))
	ls -la  $(TMPDIR)
	(cd $(TMPBASE); tar cvzf /tmp/test.tgz test)
