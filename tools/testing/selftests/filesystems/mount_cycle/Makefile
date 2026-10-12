# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := loop_cycle_test unmounted_tree_test overmount_reparent_test mount_cover_test locked_handle_test
TEST_GEN_PROGS += nsfs_rbind_loop_test overmount_ns_file_test

CFLAGS += -Wall -O2 -g $(KHDR_INCLUDES)

LOCAL_HDRS += ../readdir_hold.h

include ../../lib.mk

$(OUTPUT)/locked_handle_test: LDLIBS += -pthread
$(OUTPUT)/mount_cover_test: LDLIBS += -pthread
