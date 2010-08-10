#
# This is a simple Makefile to test some of the RAID-6 code
# from userspace.
#

CC	 = gcc
OPTFLAGS = -O2			# Adjust as desired
CFLAGS	 = -I.. -I ../../../include -g $(OPTFLAGS)
LD	 = ld
AWK	 = awk
AR	 = ar
RANLIB	 = ranlib

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

%.c: ../%.c
	cp -f $< $@

%.uc: ../%.uc
	cp -f $< $@

all:	raid6.a raid6test

raid6.a: raid6int1.o raid6int2.o raid6int4.o raid6int8.o raid6int16.o \
	 raid6int32.o \
	 raid6mmx.o raid6sse1.o raid6sse2.o \
	 raid6altivec1.o raid6altivec2.o raid6altivec4.o raid6altivec8.o \
	 raid6recov.o raid6algos.o \
	 raid6tables.o
	 rm -f $@
	 $(AR) cq $@ $^
	 $(RANLIB) $@

raid6test: test.c raid6.a
	$(CC) $(CFLAGS) -o raid6test $^

raid6altivec1.c: raid6altivec.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=1 < raid6altivec.uc > $@

raid6altivec2.c: raid6altivec.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=2 < raid6altivec.uc > $@

raid6altivec4.c: raid6altivec.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=4 < raid6altivec.uc > $@

raid6altivec8.c: raid6altivec.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=8 < raid6altivec.uc > $@

raid6int1.c: raid6int.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=1 < raid6int.uc > $@

raid6int2.c: raid6int.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=2 < raid6int.uc > $@

raid6int4.c: raid6int.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=4 < raid6int.uc > $@

raid6int8.c: raid6int.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=8 < raid6int.uc > $@

raid6int16.c: raid6int.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=16 < raid6int.uc > $@

raid6int32.c: raid6int.uc ../unroll.awk
	$(AWK) ../unroll.awk -vN=32 < raid6int.uc > $@

raid6tables.c: mktables
	./mktables > raid6tables.c

clean:
	rm -f *.o *.a mktables mktables.c raid6int.uc raid6*.c raid6test

spotless: clean
	rm -f *~
