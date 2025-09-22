#	$OpenBSD: Makefile,v 1.5 2022/12/04 19:04:38 anton Exp $

.if ${MACHINE} == "i386"
REGRESS_TARGETS+=	size

SRC=		${.CURDIR}/../../..

size:	clean
	mkdir build
	/usr/sbin/config -s ${SRC}/sys -b build \
		${SRC}/sys/arch/${MACHINE}/conf/GENERIC
	cd build; \
	make db_hangman.o; \
	if [ `size db_hangman.o | \
		awk 'BEGIN {getline} {print $$1+$$2}'` -gt 2048 ]; \
	then \
		echo 'hangman is too big!!!'; \
		false; \
	fi

clean:
	rm -rf build

.elif make(regress) || make(all)
${.TARGETS}:
	@echo Cannot run on ${MACHINE}
	@echo SKIPPED
.endif

.include <bsd.regress.mk>
