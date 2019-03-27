# $FreeBSD$

.if !target(__<bsd.init.mk>__)
.error bsd.links.mk cannot be included directly.
.endif

.if defined(NO_ROOT)
.if !defined(TAGS) || ! ${TAGS:Mpackage=*}
TAGS+=         package=${PACKAGE}
.endif
TAG_ARGS=      -T ${TAGS:[*]:S/ /,/g}
.endif

afterinstall: _installlinks
.ORDER: realinstall _installlinks
_installlinks:
.for s t in ${LINKS}
	${INSTALL_LINK} ${TAG_ARGS} ${DESTDIR}${s} ${DESTDIR}${t}
.endfor
.for s t in ${SYMLINKS}
	${INSTALL_SYMLINK} ${TAG_ARGS} ${s} ${DESTDIR}${t}
.endfor
