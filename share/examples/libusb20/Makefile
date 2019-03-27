# $FreeBSD$

PACKAGE=examples
FILESDIR=${SHAREDIR}/examples/libusb20
TARGETS=	bulk control
CFLAGS+=	-Wall

all: $(TARGETS)

bulk: bulk.o util.o
	$(CC) $(CFLAGS) -o bulk bulk.o util.o -lusb

control: control.o util.o
	$(CC) $(CFLAGS) -o control control.o util.o -lusb

clean:
	rm -f $(TARGETS) *.o *~
