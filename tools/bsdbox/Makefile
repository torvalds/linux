#$FreeBSD$
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

MAN=

.include <bsd.own.mk>
MK_SSP=	no

PROG=	bsdbox
BINDIR?=/sbin

#################################################################
#
# General notes:
#
# A number of Make variables are used to generate the crunchgen config file.
#
#  CRUNCH_SRCDIRS: lists directories to search for included programs
#  CRUNCH_PROGS:  lists programs to be included
#  CRUNCH_LIBS:  libraries to statically link with
#  CRUNCH_SHLIBS:  libraries to dynamically link with
#  CRUNCH_BUILDOPTS: generic build options to be added to every program
#  CRUNCH_BUILDTOOLS: lists programs that need build tools built in the
#	local architecture.
#
# Special options can be specified for individual programs
#  CRUNCH_SRCDIR_$(P): base source directory for program $(P)
#  CRUNCH_BUILDOPTS_$(P): additional build options for $(P)
#  CRUNCH_ALIAS_$(P): additional names to be used for $(P)
#
# By default, any name appearing in CRUNCH_PROGS or CRUNCH_ALIAS_${P}
# will be used to generate a hard link to the resulting binary.
# Specific links can be suppressed by setting
# CRUNCH_SUPPRESS_LINK_$(NAME) to 1.
#

# Define Makefile variable RESCUE
CRUNCH_BUILDOPTS+= -DRESCUE

# Don't do symlinks as part of the install
CRUNCH_GENERATE_LINKS= no

# Which sources have local-arch build tools?
# Define as blank; othrs need to override
CRUNCH_BUILDTOOLS=

###################################################################
# Programs from stock /bin
#
# WARNING: Changing this list may require adjusting
# /usr/include/paths.h as well!  You were warned!
#
CRUNCH_SRCDIRS+= bin
# These are required to be shared so login and su can run as
# setuid binaries - they use these libraries. PAM needs to be
# built dynamically or it tries to build _all_ of the modules
# statically - and that ends very badly.
CRUNCH_SHLIBS+=	-lc -lutil -lcrypt -lxo -lgpio
CRUNCH_LIBS+= -lkvm -lmemstat -lnetgraph
CRUNCH_LIBS+= -lcrypt -ledit -ll -ltermcapw

###################################################################
# Programs from standard /sbin
#
# WARNING: Changing this list may require adjusting
# /usr/include/paths.h as well!  You were warned!
#
# Note that mdmfs have their own private 'pathnames.h'
# headers in addition to the standard 'paths.h' header.
#
CRUNCH_SRCDIRS+= sbin

CRUNCH_LIBS+= -l80211 -lalias -lcam -lcursesw -ldevstat -lipsec
# Don't forget this - ifconfig, etc -adrian
CRUNCH_LIBS+= ${LDADD_ifconfig}
CRUNCH_LIBS+= -lgeom -lbsdxml -ljail -lkiconv -lmd -lsbuf -lufs

##################################################################
# Programs from stock /usr/bin
#
CRUNCH_SRCDIRS+= usr.bin
# grep
CRUNCH_LIBS+=	-lbz2

##################################################################
# Programs from stock /usr/sbin
#
CRUNCH_SRCDIRS+= usr.sbin

##################################################################

CRUNCH_SRCDIRS+= libexec

CRUNCH_LIBS+= -lm

.include "Makefile.base"
.include "Makefile.net"
.include "Makefile.hostapd"
#.include "Makefile.textproc"
.include "Makefile.login"
.include "Makefile.kld"
# telnet/telnetd are too broken to include as a crunchgen'ed binary,
# thanks to some of the horrible layering violations going on.
# .include "Makefile.telnetd"
.include "Makefile.fs"

CRUNCH_LIBS+= -lcrypto -lssl -lz

# the crunchgen build environment
.include <bsd.crunchgen.mk>

# and since it creates a program..
.include <bsd.prog.mk>
