# SPDX-License-Identifier: GPL-2.0-only
all: test
test: vsock_diag_test
vsock_diag_test: vsock_diag_test.o timeout.o control.o

CFLAGS += -g -O2 -Werror -Wall -I. -I../../include/uapi -I../../include -Wno-pointer-sign -fno-strict-overflow -fno-strict-aliasing -fno-common -MMD -U_FORTIFY_SOURCE -D_GNU_SOURCE
.PHONY: all test clean
clean:
	${RM} *.o *.d vsock_diag_test
-include *.d
