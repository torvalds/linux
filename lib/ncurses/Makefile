# $FreeBSD$

SUBDIR=	ncurses form menu panel \
	ncursesw formw menuw panelw

SUBDIR_PARALLEL=

.for subdir in ${SUBDIR:Nncurses*:N*w}
SUBDIR_DEPEND_${subdir}=	ncurses
SUBDIR_DEPEND_${subdir}w=	ncursesw
.endfor

.include <bsd.subdir.mk>
