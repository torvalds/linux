# SPDX-License-Identifier: GPL-2.0
CFLAGS += -Wall -pthread

all:

TEST_FILES     := with_stress.sh
TEST_PROGS     := test_stress.sh
TEST_GEN_PROGS = test_memcontrol
TEST_GEN_PROGS += test_kmem
TEST_GEN_PROGS += test_core
TEST_GEN_PROGS += test_freezer
TEST_GEN_PROGS += test_kill

include ../lib.mk

$(OUTPUT)/test_memcontrol: cgroup_util.c ../clone3/clone3_selftests.h
$(OUTPUT)/test_kmem: cgroup_util.c ../clone3/clone3_selftests.h
$(OUTPUT)/test_core: cgroup_util.c ../clone3/clone3_selftests.h
$(OUTPUT)/test_freezer: cgroup_util.c ../clone3/clone3_selftests.h
$(OUTPUT)/test_kill: cgroup_util.c ../clone3/clone3_selftests.h ../pidfd/pidfd.h
