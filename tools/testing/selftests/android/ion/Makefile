
INCLUDEDIR := -I. -I../../../../../drivers/staging/android/uapi/
CFLAGS := $(CFLAGS) $(INCLUDEDIR) -Wall -O2 -g

TEST_GEN_FILES := ionapp_export ionapp_import

all: $(TEST_GEN_FILES)

$(TEST_GEN_FILES): ipcsocket.c ionutils.c

TEST_PROGS := ion_test.sh

include ../../lib.mk

$(OUTPUT)/ionapp_export: ionapp_export.c ipcsocket.c ionutils.c
$(OUTPUT)/ionapp_import: ionapp_import.c ipcsocket.c ionutils.c
