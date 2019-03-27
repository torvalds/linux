# $FreeBSD$

all: ttls3

LDFLAGS=-shared -Bsymbolic --allow-shlib-undefined
CFLAGS+= -lpthread
CFLAGS+= -Wl,--rpath=${.OBJDIR}

tls-lib: elftls.S
	gcc -c -o elftls.o ${.CURDIR}/elftls.S
	ld $(LDFLAGS) elftls.o -soname libtls.so.1 -o libtls.so.1
	ln -sf libtls.so.1 libtls.so

tls-test-lib: tls-lib tls-test-lib.c
	gcc -c -o tls-test.o ${.CURDIR}/tls-test-lib.c
	ld $(LDFLAGS) tls-test.o libtls.so.1 -rpath=${.OBJDIR} -soname libtls-test.so.1 -o libtls-test.so.1

ttls3: tls-test-lib tls-test.c
	gcc $(CFLAGS) -rdynamic -o ttls3 ${.CURDIR}/tls-test.c

clean:
	rm -f *.o libtls.so* libtls-test.so* ttls3

.include <bsd.obj.mk>
