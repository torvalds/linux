INSTALL=install

prefix  ?= /usr
bindir  ?= $(prefix)/bin
mandir  ?= $(prefix)/share/man
miscdir ?= $(prefix)/share/rvgen
srcdir  ?= $(prefix)/src

PYLIB  ?= $(shell python3 -c 'import sysconfig;  print (sysconfig.get_path("purelib"))')

.PHONY: all
all:

.PHONY: clean
clean:

.PHONY: install
install:
	$(INSTALL) rvgen/automata.py -D -m 644 $(DESTDIR)$(PYLIB)/rvgen/automata.py
	$(INSTALL) rvgen/dot2c.py -D -m 644 $(DESTDIR)$(PYLIB)/rvgen/dot2c.py
	$(INSTALL) dot2c -D -m 755 $(DESTDIR)$(bindir)/
	$(INSTALL) rvgen/dot2k.py -D -m 644 $(DESTDIR)$(PYLIB)/rvgen/dot2k.py
	$(INSTALL) __main__.py -D -m 755 $(DESTDIR)$(bindir)/rvgen

	mkdir -p ${miscdir}/
	cp -rp dot2k_templates $(DESTDIR)$(miscdir)/
