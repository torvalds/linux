include ../../scripts/Makefile.include
include ../../scripts/utilities.mak
BINDIR=usr/bin
MANDIR=usr/share/man
MAN1DIR=$(MANDIR)/man1

MAN1=kvm_stat.1

A2X=a2x
a2x_path := $(call get-executable,$(A2X))

all: man

ifneq ($(findstring $(MAKEFLAGS),s),s)
  ifneq ($(V),1)
     QUIET_A2X = @echo '  A2X     '$@;
  endif
endif

%.1: %.txt
ifeq ($(a2x_path),)
	$(error "You need to install asciidoc for man pages")
else
	$(QUIET_A2X)$(A2X) --doctype manpage --format manpage $<
endif

clean:
	rm -f $(MAN1)

man: $(MAN1)

install-man: man
	install -d -m 755 $(INSTALL_ROOT)/$(MAN1DIR)
	install -m 644 kvm_stat.1 $(INSTALL_ROOT)/$(MAN1DIR)

install-tools:
	install -d -m 755 $(INSTALL_ROOT)/$(BINDIR)
	install -m 755 -p "kvm_stat" "$(INSTALL_ROOT)/$(BINDIR)/$(TARGET)"

install: install-tools install-man
.PHONY: all clean man install-tools install-man install
