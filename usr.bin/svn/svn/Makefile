# $FreeBSD$

.include "${.CURDIR:H}/Makefile.inc"

.PATH:	${SVNDIR}/svn

PROG=	svn${SVNLITE}

SRCS=	add-cmd.c auth-cmd.c blame-cmd.c cat-cmd.c changelist-cmd.c \
	checkout-cmd.c cl-conflicts.c cleanup-cmd.c commit-cmd.c \
	conflict-callbacks.c copy-cmd.c delete-cmd.c deprecated.c \
	diff-cmd.c export-cmd.c file-merge.c help-cmd.c import-cmd.c \
	info-cmd.c list-cmd.c lock-cmd.c log-cmd.c merge-cmd.c \
	mergeinfo-cmd.c mkdir-cmd.c move-cmd.c notify.c patch-cmd.c \
	propdel-cmd.c propedit-cmd.c propget-cmd.c proplist-cmd.c \
	props.c propset-cmd.c relocate-cmd.c resolve-cmd.c \
	resolved-cmd.c revert-cmd.c \
	shelve-cmd.c status-cmd.c similarity.c status.c \
	svn.c switch-cmd.c unlock-cmd.c update-cmd.c upgrade-cmd.c util.c

CFLAGS+=-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR:H} \
	-I${.CURDIR:H}/lib/libapr \
	-I${APR}/include/arch/unix \
	-I${APR}/include \
	-I${.CURDIR:H}/lib/libapr_util \
	-I${APRU}/include/private \
	-I${APRU}/include

LDADD=	-L${LIBSVN_CLIENTDIR} -lsvn_client${PIE_SUFFIX} \
	-L${LIBSVN_WCDIR} -lsvn_wc${PIE_SUFFIX} \
	-L${LIBSVN_RADIR} -lsvn_ra${PIE_SUFFIX} \
	-L${LIBSVN_RA_LOCALDIR} -lsvn_ra_local${PIE_SUFFIX} \
	-L${LIBSVN_RA_SVNDIR} -lsvn_ra_svn${PIE_SUFFIX} \
	-L${LIBSVN_RA_SERFDIR} -lsvn_ra_serf${PIE_SUFFIX} \
	-L${LIBSVN_REPOSDIR} -lsvn_repos${PIE_SUFFIX} \
	-L${LIBSVN_FSDIR} -lsvn_fs${PIE_SUFFIX} \
	-L${LIBSVN_FS_FSDIR} -lsvn_fs_fs${PIE_SUFFIX} \
	-L${LIBSVN_FS_XDIR} -lsvn_fs_x${PIE_SUFFIX} \
	-L${LIBSVN_FS_UTILDIR} -lsvn_fs_util${PIE_SUFFIX} \
	-L${LIBSVN_DELTADIR} -lsvn_delta${PIE_SUFFIX} \
	-L${LIBSVN_DIFFDIR} -lsvn_diff${PIE_SUFFIX} \
	-L${LIBSVN_SUBRDIR} -lsvn_subr${PIE_SUFFIX} \
	-L${LIBSERFDIR} -lserf${PIE_SUFFIX} \
	-L${LIBAPR_UTILDIR} -lapr-util${PIE_SUFFIX} \
	-L${LIBAPRDIR} -lapr${PIE_SUFFIX}
LIBADD+=	bsdxml sqlite3 z magic crypto ssl pthread

DPADD=	${LIBSVN_CLIENT} ${LIBSVN_WC} ${LIBSVN_RA} ${LIBSVN_RA_LOCAL} \
	${LIBSVN_RA_SVN} ${LIBSVN_RA_SERF} ${LIBSVN_REPOS} \
	${LIBSVN_FS} ${LIBSVN_FS_FS} ${LIBSVN_FS_UTIL} ${LIBSVN_DELTA} \
	${LIBSVN_DIFF} ${LIBSVN_SUBR} ${LIBSERF} ${LIBAPR_UTIL} \
	${LIBAPR}

CLEANFILES+=	svnlite.1
.if(defined(ORGANIZATION) && !empty(ORGANIZATION))
SRCS+=		freebsd-organization.h
CLEANFILES+=	freebsd-organization.h
CFLAGS+=	-I. -DHAS_ORGANIZATION_NAME
freebsd-organization.h:
	@echo "#define ORGANIZATION_NAME	\"$$(eval echo ${ORGANIZATION})\"" \
	    > freebsd-organization.h
.endif

svnlite.1: svn.1
	# Avoid the website
	sed -E 's,(^| |B|`)svn,\1svnlite,g' ${SVNDIR}/svn/svn.1 > ${.OBJDIR}/svnlite.1

.include <bsd.prog.mk>
