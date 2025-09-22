#	$OpenBSD: Makefile,v 1.7 2024/09/17 08:47:37 tb Exp $

.if !exists(/usr/local/share/tlsfuzzer)
regress:
	@echo package py3-tlsfuzzer is required for this regress
	@echo SKIPPED
.else

REGRESS_TARGETS=regress-tlsfuzzer

localhost.key localhost.crt:
	openssl req -x509 -newkey rsa -keyout localhost.key -out localhost.crt \
		-subj /CN=localhost -nodes -batch

certs: localhost.key localhost.crt

start-server:	certs
	openssl s_server -accept 4433 -groups X25519:P-256:P-521:P-384 \
		-key localhost.key -cert localhost.crt -www

CLEANFILES	+= localhost.key localhost.crt

PORT		?= 4433
SLOW		= -s
TIMING		= # -t
VERBOSE		= # -v

regress-tlsfuzzer: certs
	python3 ${.CURDIR}/tlsfuzzer.py ${SLOW} ${TIMING} ${VERBOSE}

failing: certs
	python3 ${.CURDIR}/tlsfuzzer.py -f ${SLOW} ${TIMING} ${VERBOSE}


port: certs
	python3 ${.CURDIR}/tlsfuzzer.py ${SLOW} ${TIMING} ${VERBOSE} -p ${PORT}

list:
	@python3 ${.CURDIR}/tlsfuzzer.py -l

list-failing:
	@python3 ${.CURDIR}/tlsfuzzer.py -l -f

missing:
	@python3 ${.CURDIR}/tlsfuzzer.py -m

.PHONY: all certs failing list list-failing missing port start-server

.endif

.include <bsd.regress.mk>
