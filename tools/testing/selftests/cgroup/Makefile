# SPDX-License-Identifier: GPL-2.0
CFLAGS += -Wall -pthread

all: ${HELPER_PROGS}

TEST_FILES     := with_stress.sh
TEST_PROGS     := test_stress.sh test_cpuset_prs.sh
TEST_GEN_FILES := wait_inotify
TEST_GEN_PROGS = test_memcontrol
TEST_GEN_PROGS += test_kmem
TEST_GEN_PROGS += test_core
TEST_GEN_PROGS += test_freezer
TEST_GEN_PROGS += test_kill
TEST_GEN_PROGS += test_cpu
TEST_GEN_PROGS += test_cpuset
TEST_GEN_PROGS += test_zswap

LOCAL_HDRS += $(selfdir)/clone3/clone3_selftests.h $(selfdir)/pidfd/pidfd.h

include ../lib.mk

$(OUTPUT)/test_memcontrol: cgroup_util.c
$(OUTPUT)/test_kmem: cgroup_util.c
$(OUTPUT)/test_core: cgroup_util.c
$(OUTPUT)/test_freezer: cgroup_util.c
$(OUTPUT)/test_kill: cgroup_util.c
$(OUTPUT)/test_cpu: cgroup_util.c
$(OUTPUT)/test_cpuset: cgroup_util.c
$(OUTPUT)/test_zswap: cgroup_util.c
