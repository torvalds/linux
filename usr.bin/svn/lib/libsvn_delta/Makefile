# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${SVNDIR}/libsvn_delta

INTERNALLIB=	yes
LIB=		svn_delta

SRCS=		branch.c branch_compat.c branch_migrate.c branch_nested.c \
		branch_repos.c cancel.c compat.c compose_delta.c \
		debug_editor.c default_editor.c deprecated.c \
		depth_filter_editor.c editor.c element.c \
		path_driver.c svndiff.c text_delta.c version.c xdelta.c

CFLAGS+=	-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR}/../.. \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix \
		-I${APR}/include \
		-I${.CURDIR}/../libapr_util \
		-I${APRU}/include/private \
		-I${APRU}/include

.include <bsd.lib.mk>
