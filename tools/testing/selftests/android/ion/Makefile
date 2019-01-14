
INCLUDEDIR := -I. -I../../../../../drivers/staging/android/uapi/ -I../../../../../usr/include/
CFLAGS := $(CFLAGS) $(INCLUDEDIR) -Wall -O2 -g

TEST_GEN_FILES := ionapp_export ionapp_import ionmap_test

all: $(TEST_GEN_FILES)

$(TEST_GEN_FILES): ipcsocket.c ionutils.c

TEST_PROGS := ion_test.sh

KSFT_KHDR_INSTALL := 1
top_srcdir = ../../../../..
include ../../lib.mk

$(OUTPUT)/ionapp_export: ionapp_export.c ipcsocket.c ionutils.c
$(OUTPUT)/ionapp_import: ionapp_import.c ipcsocket.c ionutils.c
$(OUTPUT)/ionmap_test: ionmap_test.c ionutils.c
