# SPDX-License-Identifier: GPL-2.0-only
include ../../../scripts/Makefile.include
include ../../../scripts/utilities.mak

INSTALL ?= install
RM ?= rm -f
RMDIR ?= rmdir --ignore-fail-on-non-empty

ifeq ($(V),1)
  Q =
else
  Q = @
endif

prefix ?= /usr/local
mandir ?= $(prefix)/man
man8dir = $(mandir)/man8

# Load targets for building eBPF helpers man page.
include ../../Makefile.helpers

MAN8_RST = $(filter-out $(HELPERS_RST),$(wildcard *.rst))

_DOC_MAN8 = $(patsubst %.rst,%.8,$(MAN8_RST))
DOC_MAN8 = $(addprefix $(OUTPUT),$(_DOC_MAN8))

man: man8 helpers
man8: $(DOC_MAN8)

RST2MAN_DEP := $(shell command -v rst2man 2>/dev/null)

$(OUTPUT)%.8: %.rst
ifndef RST2MAN_DEP
	$(error "rst2man not found, but required to generate man pages")
endif
	$(QUIET_GEN)rst2man $< > $@

clean: helpers-clean
	$(call QUIET_CLEAN, Documentation)
	$(Q)$(RM) $(DOC_MAN8)

install: man helpers-install
	$(call QUIET_INSTALL, Documentation-man)
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(man8dir)
	$(Q)$(INSTALL) -m 644 $(DOC_MAN8) $(DESTDIR)$(man8dir)

uninstall: helpers-uninstall
	$(call QUIET_UNINST, Documentation-man)
	$(Q)$(RM) $(addprefix $(DESTDIR)$(man8dir)/,$(_DOC_MAN8))
	$(Q)$(RMDIR) $(DESTDIR)$(man8dir)

.PHONY: man man8 clean install uninstall
.DEFAULT_GOAL := man
