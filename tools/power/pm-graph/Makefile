# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# Authors:
#	 Todd Brandt <todd.e.brandt@linux.intel.com>

# Prefix to the directories we're installing to
DESTDIR ?=

# Directory definitions. These are default and most probably
# do not need to be changed. Please note that DESTDIR is
# added in front of any of them

BINDIR ?=	/usr/bin
MANDIR ?=	/usr/share/man
LIBDIR ?=	/usr/lib

# Toolchain: what tools do we use, and what options do they need:
INSTALL = /usr/bin/install
INSTALL_DATA  = ${INSTALL} -m 644

all:
	@echo "Nothing to build"

install : uninstall
	$(INSTALL) -d  $(DESTDIR)$(LIBDIR)/pm-graph
	$(INSTALL) sleepgraph.py $(DESTDIR)$(LIBDIR)/pm-graph
	$(INSTALL) bootgraph.py $(DESTDIR)$(LIBDIR)/pm-graph
	$(INSTALL) -d  $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/cgskip.txt $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/freeze-callgraph.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/freeze.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/freeze-dev.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/standby-callgraph.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/standby.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/standby-dev.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/suspend-callgraph.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/suspend.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/suspend-dev.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config
	$(INSTALL_DATA) config/suspend-x2-proc.cfg $(DESTDIR)$(LIBDIR)/pm-graph/config

	$(INSTALL) -d  $(DESTDIR)$(BINDIR)
	ln -s ../lib/pm-graph/bootgraph.py $(DESTDIR)$(BINDIR)/bootgraph
	ln -s ../lib/pm-graph/sleepgraph.py $(DESTDIR)$(BINDIR)/sleepgraph

	$(INSTALL) -d  $(DESTDIR)$(MANDIR)/man8
	$(INSTALL) bootgraph.8 $(DESTDIR)$(MANDIR)/man8
	$(INSTALL) sleepgraph.8 $(DESTDIR)$(MANDIR)/man8

uninstall :
	rm -f $(DESTDIR)$(MANDIR)/man8/bootgraph.8
	rm -f $(DESTDIR)$(MANDIR)/man8/sleepgraph.8

	rm -f $(DESTDIR)$(BINDIR)/bootgraph
	rm -f $(DESTDIR)$(BINDIR)/sleepgraph

	rm -f $(DESTDIR)$(LIBDIR)/pm-graph/config/*
	if [ -d $(DESTDIR)$(LIBDIR)/pm-graph/config ] ; then \
		rmdir $(DESTDIR)$(LIBDIR)/pm-graph/config; \
	fi;
	rm -f $(DESTDIR)$(LIBDIR)/pm-graph/__pycache__/*
	if [ -d $(DESTDIR)$(LIBDIR)/pm-graph/__pycache__ ] ; then \
		rmdir $(DESTDIR)$(LIBDIR)/pm-graph/__pycache__; \
	fi;
	rm -f $(DESTDIR)$(LIBDIR)/pm-graph/*
	if [ -d $(DESTDIR)$(LIBDIR)/pm-graph ] ; then \
		rmdir $(DESTDIR)$(LIBDIR)/pm-graph; \
	fi;

help:
	@echo  'Building targets:'
	@echo  '  all		  - Nothing to build'
	@echo  '  install	  - Install the program and create necessary directories'
	@echo  '  uninstall	  - Remove installed files and directories'

.PHONY: all install uninstall help
