# $FreeBSD$
# You probably want to take -DREDEBUG out of CFLAGS, and put something like
# -O in, *after* testing (-DREDEBUG strengthens testing by enabling a lot of
# internal assertion checking).  Take -Dconst= out for an ANSI compiler.
# Do not take -DPOSIX_MISTAKE out.  REGCFLAGS isn't important to you (it's
# for my use in some special contexts).

PATHS= ${LIBC_SRCTOP}/regex ${LIBC_SRCTOP}/locale ${SRCTOP}/include
.PATH: ${PATHS}

CFLAGS+= -static -DPOSIX_MISTAKE -DREDEBUG $(REGCFLAGS)
.for incpath in ${PATHS}
CFLAGS+= -I${incpath}
.endfor

# If you have an ANSI compiler, take -o out of MKHFLAGS.  If you want
# the Berkeley __P macro, put -b in.
MKHFLAGS =

LDFLAGS =

# If you have an ANSI environment, take limits.h and stdlib.h out of
# HMISSING and take memmove out of SRCMISSING and OBJMISSING.
HMISSING =
SRCMISSING = split.c
OBJMISSING = split.o
H = cname.h regex2.h utils.h $(HMISSING)
REGSRC = regcomp.c regerror.c regexec.c regfree.c engine.c
SRC = $(REGSRC) debug.c main.c $(SRCMISSING)

# Internal stuff, should not need changing.
OBJPRODN = regcomp.o regexec.o regerror.o regfree.o
OBJS = $(OBJPRODN) debug.o main.o $(OBJMISSING)

# Stuff that matters only if you're trying to lint the package.
LINTFLAGS = -I. -Dstatic= -Dconst= -DREDEBUG
LINTC = regcomp.c regexec.c regerror.c regfree.c debug.c main.c $(SRCMISSING)
JUNKLINT =possible pointer alignment|null effect

.SUFFIXES:	.ih .h
.c.ih:
	sh mkh $(MKHFLAGS) -p $< >$@

default:	r

re:	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

o:	$(OBJPRODN)

REGEXHSRC = ../regex2.h ../reg*.c
h:	$(REGEXHSRC)
	sh mkh $(MKHFLAGS) -i _REGEX_H_ $(REGEXHSRC) >regex.tmp
	cmp -s regex.tmp regex.h 2>/dev/null || cp regex.tmp regex.h
	rm -f regex.tmp

regex.h: h

regcomp.o regexec.o regfree.o debug.o:	utils.h regex.h regex2.h
regcomp.o:	cname.h regcomp.ih
regexec.o:	engine.c engine.ih
regerror.o:	regerror.ih
regerror.o:	utils.h
debug.o:	debug.ih
main.o:	debug.ih main.ih split.ih
split.o:	split.ih

r:	re tests
	./re <tests
	./re -el <tests
	./re -er <tests

ra:	./re tests
	-./re <tests
	-./re -el <tests
	-./re -er <tests

rx:	./re tests
	./re -x <tests
	./re -x -el <tests
	./re -x -er <tests

t:	./re tests
	-time ./re <tests
	-time ./re -cs <tests
	-time ./re -el <tests
	-time ./re -cs -el <tests

l:	$(LINTC)
	lint $(LINTFLAGS) -h $(LINTC) 2>&1 | egrep -v '$(JUNKLINT)' | tee lint

clean:	tidy
	rm -f *.o *.s *.ih re

tidy:
	rm -f junk* core regex.tmp lint

spotless:	clean
	rm -f regex.h
