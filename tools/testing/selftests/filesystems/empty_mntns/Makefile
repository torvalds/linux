# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS += -lcap

TEST_GEN_PROGS := empty_mntns_test overmount_chroot_test clone3_empty_mntns_test
TEST_GEN_PROGS += internal_sb_reconfigure_test nullfs_atime_test root_readdir_test

LOCAL_HDRS += ../readdir_hold.h

include ../../lib.mk

$(OUTPUT)/empty_mntns_test: ../utils.c
$(OUTPUT)/overmount_chroot_test: ../utils.c
$(OUTPUT)/clone3_empty_mntns_test: ../utils.c
$(OUTPUT)/internal_sb_reconfigure_test: ../utils.c
$(OUTPUT)/root_readdir_test: LDLIBS += -pthread
