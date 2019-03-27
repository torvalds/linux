# ################################################################
# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ################################################################

CXXFLAGS ?= -O3
CXXFLAGS += -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow -Wstrict-aliasing=1 -Wswitch-enum -Wno-comment
CXXFLAGS += $(MOREFLAGS)
FLAGS   = $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS)

ZSTDAPI = ../../lib/zstd.h
ZSTDMANUAL = ../../doc/zstd_manual.html
LIBVER_MAJOR_SCRIPT:=`sed -n '/define ZSTD_VERSION_MAJOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < $(ZSTDAPI)`
LIBVER_MINOR_SCRIPT:=`sed -n '/define ZSTD_VERSION_MINOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < $(ZSTDAPI)`
LIBVER_PATCH_SCRIPT:=`sed -n '/define ZSTD_VERSION_RELEASE/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < $(ZSTDAPI)`
LIBVER_SCRIPT:= $(LIBVER_MAJOR_SCRIPT).$(LIBVER_MINOR_SCRIPT).$(LIBVER_PATCH_SCRIPT)
LIBVER := $(shell echo $(LIBVER_SCRIPT))


# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif


.PHONY: default
default: gen_html

.PHONY: all
all: manual

gen_html: gen_html.cpp
	$(CXX) $(FLAGS) $^ -o $@$(EXT)

$(ZSTDMANUAL): gen_html $(ZSTDAPI)
	echo "Update zstd manual in /doc"
	./gen_html $(LIBVER) $(ZSTDAPI) $(ZSTDMANUAL)

.PHONY: manual
manual: gen_html $(ZSTDMANUAL)

.PHONY: clean
clean:
	@$(RM) gen_html$(EXT)
	@echo Cleaning completed
