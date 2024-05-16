# SPDX-License-Identifier: GPL-2.0-only
all: test vsock_perf
test: vsock_test vsock_diag_test vsock_uring_test
vsock_test: vsock_test.o vsock_test_zerocopy.o timeout.o control.o util.o msg_zerocopy_common.o
vsock_diag_test: vsock_diag_test.o timeout.o control.o util.o
vsock_perf: vsock_perf.o msg_zerocopy_common.o

vsock_uring_test: LDLIBS = -luring
vsock_uring_test: control.o util.o vsock_uring_test.o timeout.o msg_zerocopy_common.o

CFLAGS += -g -O2 -Werror -Wall -I. -I../../include -I../../../usr/include -Wno-pointer-sign -fno-strict-overflow -fno-strict-aliasing -fno-common -MMD -U_FORTIFY_SOURCE -D_GNU_SOURCE
.PHONY: all test clean
clean:
	${RM} *.o *.d vsock_test vsock_diag_test vsock_perf vsock_uring_test
-include *.d
