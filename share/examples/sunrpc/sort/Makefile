#
# @(#)Makefile	2.1 88/08/11 4.0 RPCSRC
#
# $FreeBSD$
#
PACKAGE=examples
FILESDIR=${SHAREDIR}/examples/sunrpc/sort
BIN =  rsort sort_svc
GEN = sort_clnt.c sort_svc.c sort_xdr.c sort.h
LIB = -lrpclib
RPCCOM = rpcgen

all: $(BIN)

rsort: rsort.o sort_clnt.o sort_xdr.o
	$(CC) $(LDFLAGS) -o $@ rsort.o sort_clnt.o sort_xdr.o $(LIB)

rsort.o: rsort.c sort.h

sort_clnt.c:
	$(RPCCOM) -l sort.x >$@

sort_svc: sort_proc.o sort_svc.o sort_xdr.o
	$(CC) $(LDFLAGS) -o $@ sort_proc.o sort_svc.o sort_xdr.o $(LIB)

sort_proc.o: sort_proc.c sort.h

sort_svc.c:
	$(RPCCOM) -s udp sort.x >$@

sort_xdr.c:
	$(RPCCOM) -c sort.x >$@

sort.h:
	$(RPCCOM) -h sort.x >$@

clean cleanup:
	rm -f $(GEN) *.o $(BIN)

