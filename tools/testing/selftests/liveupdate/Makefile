# SPDX-License-Identifier: GPL-2.0-only

TEST_GEN_PROGS += liveupdate

TEST_GEN_PROGS_EXTENDED += luo_kexec_simple
TEST_GEN_PROGS_EXTENDED += luo_multi_session
TEST_GEN_PROGS_EXTENDED += luo_stress_sessions
TEST_GEN_PROGS_EXTENDED += luo_stress_files

TEST_FILES += do_kexec.sh

include ../lib.mk
include lib/libliveupdate.mk

CFLAGS += $(KHDR_INCLUDES)
CFLAGS += -Wall -O2 -Wno-unused-function
CFLAGS += -MD

TEST_O := $(patsubst %, %.o, $(TEST_GEN_PROGS))
TEST_O += $(patsubst %, %.o, $(TEST_GEN_PROGS_EXTENDED))

TEST_DEP_FILES := $(patsubst %.o, %.d, $(LIBLIVEUPDATE_O))
TEST_DEP_FILES += $(patsubst %.o, %.d, $(TEST_O))
-include $(TEST_DEP_FILES)

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): $(OUTPUT)/%: %.o $(LIBLIVEUPDATE_O)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) $< $(LIBLIVEUPDATE_O) $(LDLIBS) -o $@

EXTRA_CLEAN += $(TEST_O)
EXTRA_CLEAN += $(TEST_DEP_FILES)
