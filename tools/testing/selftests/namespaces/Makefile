# SPDX-License-Identifier: GPL-2.0-only
CFLAGS += -Wall -O0 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS += -lcap

TEST_GEN_PROGS := nsid_test \
		  file_handle_test \
		  init_ino_test \
		  ns_active_ref_test \
		  listns_test \
		  listns_permissions_test \
		  listns_efault_test \
		  siocgskns_test \
		  cred_change_test \
		  stress_test \
		  listns_pagination_bug \
		  regression_pidfd_setns_test

include ../lib.mk

$(OUTPUT)/ns_active_ref_test: ../filesystems/utils.c
$(OUTPUT)/listns_test: ../filesystems/utils.c
$(OUTPUT)/listns_permissions_test: ../filesystems/utils.c
$(OUTPUT)/listns_efault_test: ../filesystems/utils.c
$(OUTPUT)/siocgskns_test: ../filesystems/utils.c
$(OUTPUT)/cred_change_test: ../filesystems/utils.c
$(OUTPUT)/stress_test: ../filesystems/utils.c
$(OUTPUT)/listns_pagination_bug: ../filesystems/utils.c
$(OUTPUT)/regression_pidfd_setns_test: ../filesystems/utils.c

