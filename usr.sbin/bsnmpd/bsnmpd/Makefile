# $FreeBSD$
#
# Author: Harti Brandt <harti@freebsd.org>

.include <src.opts.mk>

CONTRIB=${SRCTOP}/contrib/bsnmp
.PATH: ${CONTRIB}/snmpd

CONFS=	snmpd.config
CONFSMODE=	600
PROG=	bsnmpd
SRCS=	main.c action.c config.c export.c trap.c trans_udp.c trans_lsock.c
SRCS+=	oid.h tree.c tree.h
XSYM=	snmpMIB begemotSnmpdModuleTable begemotSnmpd begemotTrapSinkTable \
	sysUpTime snmpTrapOID coldStart authenticationFailure \
	begemotSnmpdTransUdp begemotSnmpdTransLsock begemotSnmpdLocalPortTable \
	freeBSD freeBSDVersion
CLEANFILES= oid.h tree.c tree.h

MAN=	bsnmpd.1 snmpmod.3

MLINKS+=	snmpmod.3 FIND_OBJECT_INT.3
MLINKS+=	snmpmod.3 FIND_OBJECT_INT_LINK.3
MLINKS+=	snmpmod.3 FIND_OBJECT_INT_LINK_INDEX.3
MLINKS+=	snmpmod.3 FIND_OBJECT_OID.3
MLINKS+=	snmpmod.3 FIND_OBJECT_OID_LINK.3
MLINKS+=	snmpmod.3 FIND_OBJECT_OID_LINK_INDEX.3
MLINKS+=	snmpmod.3 INSERT_OBJECT_INT.3
MLINKS+=	snmpmod.3 INSERT_OBJECT_INT_LINK.3
MLINKS+=	snmpmod.3 INSERT_OBJECT_INT_LINK_INDEX.3
MLINKS+=	snmpmod.3 INSERT_OBJECT_OID.3
MLINKS+=	snmpmod.3 INSERT_OBJECT_OID_LINK.3
MLINKS+=	snmpmod.3 INSERT_OBJECT_OID_LINK_INDEX.3
MLINKS+=	snmpmod.3 NEXT_OBJECT_INT.3
MLINKS+=	snmpmod.3 NEXT_OBJECT_INT_LINK.3
MLINKS+=	snmpmod.3 NEXT_OBJECT_INT_LINK_INDEX.3
MLINKS+=	snmpmod.3 NEXT_OBJECT_OID.3
MLINKS+=	snmpmod.3 NEXT_OBJECT_OID_LINK.3
MLINKS+=	snmpmod.3 NEXT_OBJECT_OID_LINK_INDEX.3
MLINKS+=	snmpmod.3 bsnmpd_get_target_stats.3
MLINKS+=	snmpmod.3 bsnmpd_get_usm_stats.3
MLINKS+=	snmpmod.3 bsnmpd_reset_usm_stats.3
MLINKS+=	snmpmod.3 buf_alloc.3
MLINKS+=	snmpmod.3 buf_size.3
MLINKS+=	snmpmod.3 comm_define.3
MLINKS+=	snmpmod.3 community.3
MLINKS+=	snmpmod.3 fd_deselect.3
MLINKS+=	snmpmod.3 fd_resume.3
MLINKS+=	snmpmod.3 fd_select.3
MLINKS+=	snmpmod.3 fd_suspend.3
MLINKS+=	snmpmod.3 get_ticks.3
MLINKS+=	snmpmod.3 index_append.3
MLINKS+=	snmpmod.3 index_append_off.3
MLINKS+=	snmpmod.3 index_compare.3
MLINKS+=	snmpmod.3 index_compare_off.3
MLINKS+=	snmpmod.3 index_decode.3
MLINKS+=	snmpmod.3 ip_commit.3
MLINKS+=	snmpmod.3 ip_get.3
MLINKS+=	snmpmod.3 ip_rollback.3
MLINKS+=	snmpmod.3 ip_save.3
MLINKS+=	snmpmod.3 or_register.3
MLINKS+=	snmpmod.3 or_unregister.3
MLINKS+=	snmpmod.3 oid_commit.3
MLINKS+=	snmpmod.3 oid_get.3
MLINKS+=	snmpmod.3 oid_rollback.3
MLINKS+=	snmpmod.3 oid_save.3
MLINKS+=	snmpmod.3 oid_usmNotInTimeWindows.3
MLINKS+=	snmpmod.3 oid_usmUnknownEngineIDs.3
MLINKS+=	snmpmod.3 oid_zeroDotZero.3
MLINKS+=	snmpmod.3 reqid_allocate.3
MLINKS+=	snmpmod.3 reqid_base.3
MLINKS+=	snmpmod.3 reqid_istype.3
MLINKS+=	snmpmod.3 reqid_next.3
MLINKS+=	snmpmod.3 reqid_type.3
MLINKS+=	snmpmod.3 snmp_input_finish.3
MLINKS+=	snmpmod.3 snmp_input_start.3
MLINKS+=	snmpmod.3 snmp_output.3
MLINKS+=	snmpmod.3 snmp_pdu_auth_access.3
MLINKS+=	snmpmod.3 snmp_send_port.3
MLINKS+=	snmpmod.3 snmp_send_trap.3
MLINKS+=	snmpmod.3 snmpd_target_stat.3
MLINKS+=	snmpmod.3 snmpd_usmstats.3
MLINKS+=	snmpmod.3 start_tick.3
MLINKS+=	snmpmod.3 string_commit.3
MLINKS+=	snmpmod.3 string_free.3
MLINKS+=	snmpmod.3 string_get.3
MLINKS+=	snmpmod.3 string_get_max.3
MLINKS+=	snmpmod.3 string_rollback.3
MLINKS+=	snmpmod.3 string_save.3
MLINKS+=	snmpmod.3 systemg.3
MLINKS+=	snmpmod.3 this_tick.3
MLINKS+=	snmpmod.3 timer_start.3
MLINKS+=	snmpmod.3 timer_start_repeat.3
MLINKS+=	snmpmod.3 timer_stop.3
MLINKS+=	snmpmod.3 target_activate_address.3
MLINKS+=	snmpmod.3 target_address.3
MLINKS+=	snmpmod.3 target_delete_address.3
MLINKS+=	snmpmod.3 target_delete_notify.3
MLINKS+=	snmpmod.3 target_delete_param.3
MLINKS+=	snmpmod.3 target_first_address.3
MLINKS+=	snmpmod.3 target_first_notify.3
MLINKS+=	snmpmod.3 target_first_param.3
MLINKS+=	snmpmod.3 target_flush_all.3
MLINKS+=	snmpmod.3 target_next_address.3
MLINKS+=	snmpmod.3 target_next_notify.3
MLINKS+=	snmpmod.3 target_next_param.3
MLINKS+=	snmpmod.3 target_new_address.3
MLINKS+=	snmpmod.3 target_new_notify.3
MLINKS+=	snmpmod.3 target_new_param.3
MLINKS+=	snmpmod.3 target_notify.3
MLINKS+=	snmpmod.3 target_param.3
MLINKS+=	snmpmod.3 usm_delete_user.3
MLINKS+=	snmpmod.3 usm_find_user.3
MLINKS+=	snmpmod.3 usm_first_user.3
MLINKS+=	snmpmod.3 usm_flush_users.3
MLINKS+=	snmpmod.3 usm_next_user.3
MLINKS+=	snmpmod.3 usm_new_user.3
MLINKS+=	snmpmod.3 usm_user.3

FILESGROUPS= BMIBS DEFS

BMIBS=	FOKUS-MIB.txt BEGEMOT-MIB.txt BEGEMOT-SNMPD.txt
BMIBSDIR= ${SHAREDIR}/snmp/mibs
DEFS=	tree.def
DEFSDIR= ${SHAREDIR}/snmp/defs

CFLAGS+= -I${CONTRIB}/lib -I${CONTRIB}/snmpd -I. -DUSE_LIBBEGEMOT
CFLAGS+= -DHAVE_STDINT_H -DHAVE_INTTYPES_H -DHAVE_ERR_H -DHAVE_STRLCPY
LIBADD=	begemot bsnmp

LDFLAGS= -Wl,-export-dynamic

.if ${MK_OPENSSL} != "no"
CFLAGS+= -DHAVE_LIBCRYPTO
.endif

.if ${MK_TCP_WRAPPERS} != "no"
CFLAGS+=	-DUSE_TCPWRAPPERS
LIBADD+=	wrap
.endif

oid.h: tree.def Makefile
	gensnmptree -I${CONTRIB}/lib -e ${XSYM} < ${.ALLSRC:M*.def} > ${.TARGET}

.ORDER: tree.c tree.h
tree.h: .NOMETA
tree.c tree.h: tree.def
	gensnmptree -I${CONTRIB}/lib -l -f < ${.ALLSRC}

MANFILTER=	sed -e 's%@MODPATH@%${LIBDIR}/%g'		\
		    -e 's%@DEFPATH@%${DEFSDIR}/%g'		\
		    -e 's%@MIBSPATH@%${BMIBSDIR}/%g'

.include <bsd.prog.mk>
