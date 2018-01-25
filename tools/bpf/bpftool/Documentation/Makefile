include ../../../scripts/Makefile.include
include ../../../scripts/utilities.mak

INSTALL ?= install
RM ?= rm -f

# Make the path relative to DESTDIR, not prefix
ifndef DESTDIR
prefix?=$(HOME)
endif
mandir ?= $(prefix)/share/man
man8dir = $(mandir)/man8

MAN8_RST = $(wildcard *.rst)

_DOC_MAN8 = $(patsubst %.rst,%.8,$(MAN8_RST))
DOC_MAN8 = $(addprefix $(OUTPUT),$(_DOC_MAN8))

man: man8
man8: $(DOC_MAN8)

$(OUTPUT)%.8: %.rst
	rst2man $< > $@

clean:
	$(call QUIET_CLEAN, Documentation) $(RM) $(DOC_MAN8)

install: man
	$(call QUIET_INSTALL, Documentation-man) \
		$(INSTALL) -d -m 755 $(DESTDIR)$(man8dir); \
		$(INSTALL) -m 644 $(DOC_MAN8) $(DESTDIR)$(man8dir);

.PHONY: man man8 clean install
.DEFAULT_GOAL := man
