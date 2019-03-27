# $FreeBSD$

NEW_DIR!=	make -C ${.CURDIR}/../../../../usr.bin/make -V .OBJDIR

all:
	rm -rf /tmp/${USER}.make.test
	env MAKE_PROG=${NEW_DIR}/make ${.SHELL} ./all.sh

.include <bsd.obj.mk>
