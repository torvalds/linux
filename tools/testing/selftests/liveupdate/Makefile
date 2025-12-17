# SPDX-License-Identifier: GPL-2.0-only

LIB_C += luo_test_utils.c

TEST_GEN_PROGS += liveupdate

TEST_GEN_PROGS_EXTENDED += luo_kexec_simple
TEST_GEN_PROGS_EXTENDED += luo_multi_session

TEST_FILES += do_kexec.sh

include ../lib.mk

CFLAGS += $(KHDR_INCLUDES)
CFLAGS += -Wall -O2 -Wno-unused-function
CFLAGS += -MD

LIB_O := $(patsubst %.c, $(OUTPUT)/%.o, $(LIB_C))
TEST_O := $(patsubst %, %.o, $(TEST_GEN_PROGS))
TEST_O += $(patsubst %, %.o, $(TEST_GEN_PROGS_EXTENDED))

TEST_DEP_FILES := $(patsubst %.o, %.d, $(LIB_O))
TEST_DEP_FILES += $(patsubst %.o, %.d, $(TEST_O))
-include $(TEST_DEP_FILES)

$(LIB_O): $(OUTPUT)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): $(OUTPUT)/%: %.o $(LIB_O)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) $< $(LIB_O) $(LDLIBS) -o $@

EXTRA_CLEAN += $(LIB_O)
EXTRA_CLEAN += $(TEST_O)
EXTRA_CLEAN += $(TEST_DEP_FILES)
