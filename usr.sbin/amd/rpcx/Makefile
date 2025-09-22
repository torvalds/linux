
RPCGEN = rpcgen -C

AMQ = amq.h amq_clnt.c amq_svc.c amq_xdr.c
NFS = nfs_prot.h nfs_prot_svc.c nfs_prot_xdr.c

NFSX = /usr/include/rpcsvc/nfs_prot.x

all: ${AMQ} ${NFS}

.SUFFIXES:
.SUFFIXES: .x .h _xdr.c _clnt.c .c

.x.h:
	${RPCGEN} -o $@ -h $<
.x_xdr.c:
	${RPCGEN} -o $@ -c -i 100 $<
.x_clnt.c:
	${RPCGEN} -o $@ -l $<

amq_svc.c: amq.x $*.ed
	${RPCGEN} -o $@.tmp -m amq.x
	ed -s $@.tmp <$*.ed
	mv $@.tmp $@

nfs_prot_svc.c: nfs_prot.x $*.ed
	${RPCGEN} -o $@.tmp -m nfs_prot.x
	ed -s $@.tmp <$*.ed
	mv $@.tmp $@

nfs_prot.x: ${NFSX} $*.ed
	cat ${NFSX} >$@.tmp
	ed -s $@.tmp <$*.ed
	mv $@.tmp $@
