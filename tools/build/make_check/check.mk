# $FreeBSD$

all:
	${MK} ${MK_ARG}

.if exists(${.OBJDIR}/../../../usr.bin/make/make)
MK=	${.OBJDIR}/../../../usr.bin/make/make
new:
	${MK} ${MK_ARG} 2>&1 | tee out-new
	@echo "-=-=-=-=-=-"
	make ${MK_ARG} 2>&1 | tee out-old
	@echo "-=-=-=-=-=-"
	diff -s out-old out-new
.else
MK=	make
.endif
MK_ARG=	-C ${.CURDIR}

.include <bsd.obj.mk>
