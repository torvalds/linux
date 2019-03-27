# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${SVNDIR}/libsvn_client

INTERNALLIB=	yes
LIB=		svn_client

SRCS=		add.c blame.c cat.c changelist.c checkout.c cleanup.c \
		cmdline.c commit_util.c commit.c compat_providers.c \
		conflicts.c copy_foreign.c copy.c ctx.c delete.c deprecated.c \
		diff_local.c diff_summarize.c diff.c export.c externals.c \
		import.c info.c iprops.c list.c locking_commands.c log.c \
		merge_elements.c merge.c mergeinfo.c mtcc.c patch.c \
		prop_commands.c ra.c relocate.c repos_diff.c resolved.c \
		revert.c revisions.c shelve.c status.c switch.c update.c \
		upgrade.c url.c util.c version.c

CFLAGS+=	-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR}/../.. \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix \
		-I${APR}/include \
		-I${.CURDIR}/../libapr_util \
		-I${APRU}/include/private \
		-I${APRU}/include

.include <bsd.lib.mk>
