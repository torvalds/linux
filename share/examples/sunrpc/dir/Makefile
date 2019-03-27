#
# @(#)Makefile	2.1 88/08/02 4.0 RPCSRC
#
# $FreeBSD$
#
PACKAGE=examples
FILESDIR=${SHAREDIR}/examples/sunrpc/dir
BIN = dir_svc rls
GEN = dir_clnt.c dir_svc.c dir_xdr.c dir.h
LIB = -lrpclib
RPCCOM = rpcgen

all: $(BIN)

$(GEN): dir.x
	$(RPCCOM) dir.x

dir_svc: dir_proc.o dir_svc.o dir_xdr.o
	$(CC) -o $@ dir_proc.o dir_svc.o dir_xdr.o $(LIB)

rls: rls.o dir_clnt.o dir_xdr.o
	$(CC) -o $@ rls.o dir_clnt.o dir_xdr.o $(LIB)

rls.o: rls.c dir.h

dir_proc.o: dir_proc.c dir.h

clean cleanup:
	rm -f $(GEN) *.o $(BIN)

