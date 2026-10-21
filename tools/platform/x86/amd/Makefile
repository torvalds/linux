# SPDX-License-Identifier: GPL-2.0
ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

# Include common tools build infrastructure
include $(srctree)/tools/scripts/Makefile.include

CC		= $(CROSS_COMPILE)gcc
BUILD_OUTPUT	:= $(CURDIR)
PREFIX		?= /usr
DESTDIR		?=

ifeq ("$(origin O)", "command line")
	BUILD_OUTPUT := $(O)
endif

# Include paths: tools/include has linux/kernel.h with ARRAY_SIZE
INCLUDES	= -I$(srctree)/tools/include
INCLUDES	+= -I$(srctree)/tools/include/uapi
INCLUDES	+= -I$(srctree)/include/uapi
INCLUDES	+= -I$(srctree)/include

override CFLAGS += -O2 -Wall -Wextra -D_GNU_SOURCE $(INCLUDES)
override CFLAGS += -D_FILE_OFFSET_BITS=64
override CFLAGS += -D__EXPORTED_HEADERS__

TARGETS		= test_amd_pmf

all: $(TARGETS)

test_amd_pmf: test-pmf.c
	$(QUIET_CC)$(CC) $(CFLAGS) $< -o $(BUILD_OUTPUT)/$@ $(LDFLAGS)

.PHONY: clean
clean:
	$(call QUIET_CLEAN, test_amd_pmf)$(RM) $(BUILD_OUTPUT)/test_amd_pmf

.PHONY: install
install: $(TARGETS)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(BUILD_OUTPUT)/test_amd_pmf $(DESTDIR)$(PREFIX)/bin/test_amd_pmf

.PHONY: help
help:
	@echo "AMD Platform Tools Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all           - Build all tools (default)"
	@echo "  test_amd_pmf  - Build the PMF test tool"
	@echo "  clean         - Remove built files"
	@echo "  install       - Install tools to $(PREFIX)/bin"
	@echo ""
	@echo "Variables:"
	@echo "  O=<dir>        - Build output directory"
	@echo "  PREFIX         - Installation prefix (default: /usr)"
	@echo "  DESTDIR        - Destination directory for install"
