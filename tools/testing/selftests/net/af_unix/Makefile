top_srcdir := ../../../../..
include $(top_srcdir)/scripts/Makefile.compiler

cc-option = $(call __cc-option, $(CC),,$(1),$(2))

CFLAGS += $(KHDR_INCLUDES) -Wall $(call cc-option,-Wflex-array-member-not-at-end)

TEST_GEN_PROGS := \
	diag_uid \
	msg_oob \
	scm_inq \
	scm_pidfd \
	scm_rights \
	scm_rights_denial_lsm \
	so_peek_off \
	unix_connect \
	unix_connreset \
	unix_listen \
# end of TEST_GEN_PROGS

TEST_GEN_FILES := scm_rights_denial_lsm.bpf.o

include ../../lib.mk
include ../bpf.mk

$(OUTPUT)/scm_rights_denial_lsm: $(BPFOBJ)
$(OUTPUT)/scm_rights_denial_lsm: CFLAGS += -I$(SCRATCH_DIR)/include
$(OUTPUT)/scm_rights_denial_lsm: LDLIBS += -lelf -lz
