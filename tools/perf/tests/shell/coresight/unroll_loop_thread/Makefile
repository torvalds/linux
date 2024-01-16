# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021
include ../Makefile.miniconfig

# Binary to produce
BIN=unroll_loop_thread
# Any linking/libraries needed for the binary - empty if none needed
LIB=-pthread

all: $(BIN)

$(BIN): $(BIN).c
ifdef CORESIGHT
ifeq ($(ARCH),arm64)
# Build line
	$(Q)$(CC) $(BIN).c -o $(BIN) $(LIB)
endif
endif

install-tests: all
ifdef CORESIGHT
ifeq ($(ARCH),arm64)
# Install the test tool in the right place
	$(call QUIET_INSTALL, tests) \
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/$(INSTDIR_SUB)/$(BIN)'; \
		$(INSTALL) $(BIN) '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/$(INSTDIR_SUB)/$(BIN)/$(BIN)'
endif
endif

clean:
	$(Q)$(RM) -f $(BIN)

.PHONY: all clean install-tests
