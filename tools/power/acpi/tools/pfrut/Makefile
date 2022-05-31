# SPDX-License-Identifier: GPL-2.0+

include ../../Makefile.config

TOOL = pfrut
EXTRA_INSTALL = install-man
EXTRA_UNINSTALL = uninstall-man

CFLAGS += -Wall -O2
CFLAGS += -DPFRUT_HEADER='"../../../../../include/uapi/linux/pfrut.h"'
LDFLAGS += -luuid

TOOL_OBJS = \
	pfrut.o

include ../../Makefile.rules

install-man: $(srctree)/man/pfrut.8
	$(ECHO) "  INST    " pfrut.8
	$(QUIET) $(INSTALL_DATA) -D $< $(DESTDIR)$(mandir)/man8/pfrut.8
uninstall-man:
	$(ECHO) "  UNINST  " pfrut.8
	$(QUIET) rm -f $(DESTDIR)$(mandir)/man8/pfrut.8
