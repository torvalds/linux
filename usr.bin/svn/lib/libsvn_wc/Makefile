# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${SVNDIR}/libsvn_wc

INTERNALLIB=	yes
LIB=		svn_wc

SRCS=		adm_crawler.c adm_files.c adm_ops.c \
		ambient_depth_filter_editor.c cleanup.c conflicts.c \
		context.c copy.c crop.c delete.c deprecated.c diff_editor.c \
		diff_local.c entries.c externals.c info.c lock.c merge.c \
		node.c old-and-busted.c props.c questions.c relocate.c \
		revert.c revision_status.c status.c translate.c \
		tree_conflicts.c update_editor.c upgrade.c util.c \
		wc_db.c wc_db_pristine.c wc_db_update_move.c wc_db_util.c \
		wc_db_wcroot.c wcroot_anchor.c workqueue.c

CFLAGS+=	-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR}/../.. \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix \
		-I${APR}/include \
		-I${.CURDIR}/../libapr_util \
		-I${APRU}/include/private \
		-I${APRU}/include

.include <bsd.lib.mk>
