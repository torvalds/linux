#	From: @(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD$

# The following modules do not build/install:
# 10.gdb

# The following modules do not apply to FreeBSD:
# 07.pascal 08.f77 09.f77io

# The following previously encumbered files have not yet been added to
# the tree:
# 11.adb

SUBDIR=	title \
	contents \
	01.cacm \
	02.implement \
	03.iosys \
	04.uprog \
	05.sysman \
	06.Clang \
	12.make \
	15.yacc \
	16.lex \
	17.m4 \
	18.gprof \
	20.ipctut \
	21.ipc

# The following modules don't appear in the O'Reilly book, but
# are in the 4.4BSD distribution.
SUBDIR+=22.rpcgen \
	23.rpc \
	24.xdr \
	25.xdrrfc \
	26.rpcrfc \
	27.nfsrpc

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
