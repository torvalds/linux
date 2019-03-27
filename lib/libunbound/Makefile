# $FreeBSD$

PACKAGE=lib${LIB}
# Vendor sources and generated files
LDNSDIR= ${SRCTOP}/contrib/ldns
UNBOUNDDIR= ${SRCTOP}/contrib/unbound

# Hold my beer and watch this
.PATH: ${UNBOUNDDIR} ${UNBOUNDDIR}/cachedb ${UNBOUNDDIR}/dns64 ${UNBOUNDDIR}/iterator ${UNBOUNDDIR}/sldns ${UNBOUNDDIR}/libunbound ${UNBOUNDDIR}/services ${UNBOUNDDIR}/services/cache ${UNBOUNDDIR}/util ${UNBOUNDDIR}/util/data ${UNBOUNDDIR}/respip ${UNBOUNDDIR}/util/storage ${UNBOUNDDIR}/validator 

LIB=	unbound
PRIVATELIB=
PACKAGE=	unbound

CFLAGS+= -I${UNBOUNDDIR} -I${LDNSDIR} -I${.OBJDIR}

SRCS=	alloc.c as112.c authzone.c autotrust.c cachedb.c config_file.c \
	configlexer.l configparser.y context.c dname.c dns.c dns64.c \
	dnstree.c edns.c fptr_wlist.c infra.c iter_delegpt.c iter_donotq.c \
	iter_fwd.c iter_hints.c iter_priv.c iter_resptype.c iter_scrub.c \
	iter_utils.c iterator.c keyraw.c libunbound.c libworker.c \
	listen_dnsport.c localzone.c locks.c log.c lookup3.c lruhash.c \
	mesh.c mini_event.c modstack.c module.c msgencode.c msgparse.c \
	msgreply.c net_help.c netevent.c outbound_list.c outside_network.c \
	packed_rrset.c parse.c parseutil.c random.c rbtree.c redis.c \
	regional.c respip.c rrdef.c rrset.c rtt.c sbuffer.c slabhash.c \
	str2wire.c tcp_conn_limit.c timehist.c tube.c ub_event_pluggable.c \
	val_anchor.c val_kcache.c val_kentry.c val_neg.c val_nsec.c \
	val_nsec3.c val_secalgo.c val_sigcrypt.c val_utils.c validator.c \
	view.c winsock_event.c wire2str.c

WARNS?=	3
NO_WTHREAD_SAFETY= true

LIBADD=	ssl crypto pthread

# Misnamed file in upstream source
configlexer.l: configlexer.lex
	${CP} ${.ALLSRC} ${.TARGET}
CLEANFILES+= configlexer.l

# Symbol prefix for lex and yacc
LFLAGS= -Pub_c_
YFLAGS= -pub_c_ -d

.include <bsd.lib.mk>
