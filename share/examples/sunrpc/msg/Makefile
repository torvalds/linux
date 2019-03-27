#
# @(#)Makefile	2.1 88/08/11 4.0 RPCSRC
#
# $FreeBSD$
#
PACKAGE=examples
FILESDIR=${SHAREDIR}/examples/sunrpc/msg
BIN = printmsg msg_svc rprintmsg
GEN = msg_clnt.c msg_svc.c msg.h
LIB = -lrpclib
RPCCOM = rpcgen

all: $(BIN)

#
# This is the non-networked version of the program
#
printmsg: printmsg.o
	$(CC) -o $@ printmsg.o

#
# note: no xdr routines are generated here, due this service's
#       use of basic data types.
#
$(GEN): msg.x
	$(RPCCOM) msg.x

msg_svc: msg_proc.o msg_svc.o
	$(CC) -o $@ msg_proc.o msg_svc.o $(LIB)

rprintmsg: rprintmsg.o msg_clnt.o
	$(CC) -o $@ rprintmsg.o msg_clnt.o $(LIB)

rprintmsg.o: rprintmsg.c msg.h

msg_proc.o: msg_proc.c msg.h

clean cleanup:
	rm -f $(GEN) *.o $(BIN)

