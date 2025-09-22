# Generated automatically from Makefile.in by configure.
#
# $Id: Makefile,v 1.1 2000/10/10 20:40:26 beck Exp $
#
## Makefile for mkisofs.

#### Start of system configuration section. ####

srcdir = .

CC = gcc

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA = ${INSTALL} -m 644

LIBS = 

prefix = /usr/local
exec_prefix = $(prefix)

bindir = $(exec_prefix)/bin

# Where to put the manual pages.
mandir = $(prefix)/man/man8
# Extension (not including `.') for the manual page filenames.
manext = 8

#### End of system configuration section. ####

CFLAGS=-g -O2 -I.
LDFLAGS=

all: dump isodump isovfy isoinfo

install: Makefile isodump isovfy isoinfo isoinfo.8
	[ -d $(bindir) ] || mkdir $(bindir)
	[ -d $(mandir) ] || mkdir $(mandir)
	$(INSTALL_PROGRAM) isodump $(bindir)/isodump
	$(INSTALL_PROGRAM) isovfy  $(bindir)/isovfy
	$(INSTALL_PROGRAM) isoinfo $(bindir)/isoinfo
	-$(INSTALL_DATA) $(srcdir)/isoinfo.8 $(mandir)/isoinfo.$(manext)

config.h: ../configure
	(cd ..; ./configure)

Makefile: Makefile.in ../configure
	(cd ..; ./configure)

dump: Makefile ../config.h dump.c
	${CC} $(CFLAGS) -o dump $(srcdir)/dump.c
isodump: Makefile ../config.h isodump.c
	${CC} $(CFLAGS) -o isodump $(srcdir)/isodump.c
isovfy: Makefile isovfy.c
	${CC} $(CFLAGS) -o isovfy $(srcdir)/isovfy.c
isoinfo:Makefile ../config.h isoinfo.c
	${CC} $(CFLAGS) -o isoinfo $(srcdir)/isoinfo.c

clean:
	rm -f dump isodump isovfy isoinfo
