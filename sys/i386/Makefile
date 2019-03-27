# $FreeBSD$
#	@(#)Makefile	8.1 (Berkeley) 6/11/93

# Makefile for i386 links, tags file

# SYS is normally set in Make.tags.inc
SYS=/sys

TAGDIR=	i386

.include "../kern/Make.tags.inc"

all:
	@echo "make links or tags only"

# Directories in which to place i386 tags links
DI386=	apm i386 include isa linux

links::
	-for i in ${COMMDIR1}; do \
	    (cd $$i && { rm -f tags; ln -s ../${TAGDIR}/tags tags; }) done
	-for i in ${COMMDIR2}; do \
	    (cd $$i && { rm -f tags; ln -s ../../${TAGDIR}/tags tags; }) done
	-for i in ${DI386}; do \
	    (cd $$i && { rm -f tags; ln -s ../tags tags; }) done

SI386=	${SYS}/i386/acpica/*.[ch] ${SYS}/i386/bios/*.[ch] \
	${SYS}/i386/i386/*.[ch] ${SYS}/i386/include/*.[ch] \
	${SYS}/i386/isa/*.[ch] ${SYS}/i386/linux/*.[ch] \
	${SYS}/i386/pci/*.[ch]
AI386=	${SYS}/i386/i386/*.s

tags::
	-ctags -wdt ${COMM} ${SI386}
	egrep "^ENTRY(.*)|^ALTENTRY(.*)" ${AI386} | \
	    sed "s;\([^:]*\):\([^(]*\)(\([^, )]*\)\(.*\);\3 \1 /^\2(\3\4$$/;" \
		>> tags
	sort -o tags tags
	chmod 444 tags
