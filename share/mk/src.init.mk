# $FreeBSD$

.if !target(__<src.init.mk>__)
__<src.init.mk>__:

.if !target(buildenv)
buildenv: .PHONY
	${_+_}@env BUILDENV_DIR=${.CURDIR} ${MAKE} -C ${SRCTOP} buildenv
.endif

.endif	# !target(__<src.init.mk>__)
