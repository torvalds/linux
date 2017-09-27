# Makefile for mount selftests.
CFLAGS = -Wall \
         -O2

TEST_GEN_PROGS := unprivileged-remount-test

include ../lib.mk

override RUN_TESTS := if [ -f /proc/self/uid_map ] ; \
		      then	\
				./unprivileged-remount-test ; \
		      else	\
				echo "WARN: No /proc/self/uid_map exist, test skipped." ; \
		      fi
override EMIT_TESTS := echo "$(RUN_TESTS)"

