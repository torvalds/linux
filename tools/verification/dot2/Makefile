INSTALL=install

prefix  ?= /usr
bindir  ?= $(prefix)/bin
mandir  ?= $(prefix)/share/man
miscdir ?= $(prefix)/share/dot2
srcdir  ?= $(prefix)/src

PYLIB  ?= $(shell python3 -c 'import sysconfig;  print (sysconfig.get_path("purelib"))')

.PHONY: all
all:

.PHONY: clean
clean:

.PHONY: install
install:
	$(INSTALL) automata.py -D -m 644 $(DESTDIR)$(PYLIB)/dot2/automata.py
	$(INSTALL) dot2c.py -D -m 644 $(DESTDIR)$(PYLIB)/dot2/dot2c.py
	$(INSTALL) dot2c -D -m 755 $(DESTDIR)$(bindir)/
	$(INSTALL) dot2k.py -D -m 644 $(DESTDIR)$(PYLIB)/dot2/dot2k.py
	$(INSTALL) dot2k -D -m 755 $(DESTDIR)$(bindir)/

	mkdir -p ${miscdir}/
	cp -rp dot2k_templates $(DESTDIR)$(miscdir)/
