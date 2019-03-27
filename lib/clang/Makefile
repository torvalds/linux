# $FreeBSD$

.include <src.opts.mk>

# These have to be built in order.
SUBDIR=		libllvm
SUBDIR+=	libclang
.if ${MK_LLDB} != "no"
SUBDIR+=	liblldb
.endif
SUBDIR+=	headers

.include <bsd.subdir.mk>
