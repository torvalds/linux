# SPDX-License-Identifier: GPL-2.0-only
CFLAGS += -Wall -O2 -Wno-unused-function
CFLAGS += -D_GNU_SOURCE
LDFLAGS += -pthread

TEST_GEN_PROGS :=
TEST_GEN_PROGS += fd-001-lookup
TEST_GEN_PROGS += fd-002-posix-eq
TEST_GEN_PROGS += fd-003-kthread
TEST_GEN_PROGS += proc-loadavg-001
TEST_GEN_PROGS += proc-pid-vm
TEST_GEN_PROGS += proc-self-map-files-001
TEST_GEN_PROGS += proc-self-map-files-002
TEST_GEN_PROGS += proc-self-syscall
TEST_GEN_PROGS += proc-self-wchan
TEST_GEN_PROGS += proc-subset-pid
TEST_GEN_PROGS += proc-tid0
TEST_GEN_PROGS += proc-uptime-001
TEST_GEN_PROGS += proc-uptime-002
TEST_GEN_PROGS += read
TEST_GEN_PROGS += self
TEST_GEN_PROGS += setns-dcache
TEST_GEN_PROGS += setns-sysvipc
TEST_GEN_PROGS += thread-self
TEST_GEN_PROGS += proc-multiple-procfs
TEST_GEN_PROGS += proc-fsconfig-hidepid

include ../lib.mk
