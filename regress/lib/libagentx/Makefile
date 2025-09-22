# $OpenBSD: Makefile,v 1.9 2021/08/29 15:15:45 martijn Exp $

.if ! exists(/usr/local/sbin/snmpd)
regress:
	@printf "Install net-snmp package to run this regress.\n"
	@printf "SKIPPED\n"
.endif

PROG=			agentx
SRCS=			main.c log.c
CFLAGS+=		-Wall
NOMAN=			yes
LDADD=			-lagentx -levent

REGRESS_SETUP_ONCE=	start
REGRESS_TARGETS=
REGRESS_CLEANUP=	stop
CLEANFILES=

SNMPD_CMD=		/usr/local/sbin/snmpd -r -C -c snmpd.conf -Le
UID!!=			id -u
.if ${UID} == 0
SNMPD_CMD+=		-u _netsnmp
.endif

NOOBJECT=		No Such Object available on this agent at this OID
NOINSTANCE=		No Such Instance currently exists at this OID

CLEANFILES+=		snmpd.conf
snmpd.conf: Makefile
	@printf "agentaddress udp:127.0.0.1:6161\n" > snmpd.conf
	@printf "rwcommunity public\n" >> snmpd.conf
	@printf "master agentx\n" >> snmpd.conf
	@printf "agentXSocket agentx.sock\n" >> snmpd.conf

CLEANFILES+=		agentx.sock
start: stop snmpd.conf agentx
	${SNMPD_CMD}
	# XXX Race in net-snmpd, socket exists before data is available.
	sleep 1
	./agentx agentx.sock

stop:
	-pkill -xf "${SNMPD_CMD}"

REGRESS_TARGETS+=	walk
CLEANFILES+=		walk.run.out walk.run.err
walk:
	snmp walk -v2c -cpublic 127.0.0.1:6161 openbsd.100 > walk.run.out 2> walk.run.err || true
	diff -up ${.CURDIR}/walk.out walk.run.out
	diff -up ${.CURDIR}/walk.err walk.run.err

REGRESS_TARGETS+=	get_nosuchobject
get_nosuchobject:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.0)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_nosuchobject_0
get_nosuchobject_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.0.0)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_nosuchobject_tail
get_nosuchobject_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.0.0.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_nosuchobject_index
get_nosuchobject_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.0.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_nosuchobject_parent
get_nosuchobject_parent:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_scalarinteger
get_scalarinteger:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarinteger_0
get_scalarinteger_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.1.0)" = "INTEGER: 1" ]

REGRESS_TARGETS+=	get_scalarinteger_tail
get_scalarinteger_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.1.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarinteger_index
get_scalarinteger_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarstring
get_scalarstring:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.2)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarstring_0
get_scalarstring_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.2.0)" = "STRING: scalar-string" ]

REGRESS_TARGETS+=	get_scalarstring_tail
get_scalarstring_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.2.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarstring_index
get_scalarstring_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.2.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaroid
get_scalaroid:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.4)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaroid_0
get_scalaroid_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.4.0)" = "OID: openBSD" ]

REGRESS_TARGETS+=	get_scalaroid_tail
get_scalaroid_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.4.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaroid_index
get_scalaroid_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.4.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaripaddress
get_scalaripaddress:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.5)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaripaddress_0
get_scalaripaddress_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.5.0)" = "IpAddress: 127.0.0.1" ]

REGRESS_TARGETS+=	get_scalaripaddress_0
get_scalaripaddress_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.5.0)" = "IpAddress: 127.0.0.1" ]

REGRESS_TARGETS+=	get_scalaripaddress_tail
get_scalaripaddress_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.5.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaripaddress_index
get_scalaripaddress_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.5.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarcounter32
get_scalarcounter32:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.6)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarcounter32_0
get_scalarcounter32_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.6.0)" = "Counter32: 1" ]

REGRESS_TARGETS+=	get_scalarcounter32_tail
get_scalarcounter32_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.6.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarcounter32_index
get_scalarcounter32_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.6.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalargauge32
get_scalargauge32:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.7)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalargauge32_0
get_scalargauge32_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.7.0)" = "Gauge32: 1" ]

REGRESS_TARGETS+=	get_scalargauge32_tail
get_scalargauge32_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.7.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalargauge32_index
get_scalargauge32_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.7.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalartimeticks
get_scalartimeticks:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.8)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalartimeticks
get_scalartimeticks:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.8)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalartimeticks_0
get_scalartimeticks_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.8.0)" = "Timeticks: (1) 0:00:00.01" ]

REGRESS_TARGETS+=	get_scalartimeticks_tail
get_scalartimeticks_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.8.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalartimeticks_index
get_scalartimeticks_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.8.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaropaque
get_scalaropaque:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.9)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaropaque_0
get_scalaropaque_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.9.0)" = "Opaque: 6382179" ]

REGRESS_TARGETS+=	get_scalaropaque_tail
get_scalaropaque_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.9.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalaropaque_index
get_scalaropaque_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.9.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarcounter64
get_scalarcounter64:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.10)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarcounter64_0
get_scalarcounter64_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.10.0)" = "Counter64: 1" ]

REGRESS_TARGETS+=	get_scalarcounter64_tail
get_scalarcounter64_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.10.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_scalarcounter64_index
get_scalarcounter64_index:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.10.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint_table
get_indexint_table:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexint_entry
get_indexint_entry:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexint_object
get_indexint_object:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint_0
get_indexint_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11.1.1.0)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint_1
get_indexint_1:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11.1.1.1)" = "INTEGER: 1" ]

REGRESS_TARGETS+=	get_indexint_1_tail
get_indexint_1_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11.1.1.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint_16
get_indexint_16:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11.1.1.16)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint_16_tail
get_indexint_16_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.11.1.1.16.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint2_table
get_indexint2_table:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexint2_entry
get_indexint2_entry:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexint2_object
get_indexint2_object:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint2_0
get_indexint2_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.0)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint2_0_0
get_indexint2_0_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.0.0)" = "INTEGER: 0" ]

REGRESS_TARGETS+=	get_indexint2_0_0_tail
get_indexint2_0_0_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.0.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint2_0_1
get_indexint2_0_1:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.0.1)" = "INTEGER: 1" ]

REGRESS_TARGETS+=	get_indexint2_0_2
get_indexint2_0_2:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.0.2)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint2_1_0
get_indexint2_1_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.1.0)" = "INTEGER: 2" ]

REGRESS_TARGETS+=	get_indexint2_1_0_tail
get_indexint2_1_0_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.1.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexint2_8_1
get_indexint2_8_1:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.8.1)" = "INTEGER: 17" ]

REGRESS_TARGETS+=	get_indexint2_9_0
get_indexint2_9_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.12.1.1.9.0)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr_table
get_indexstr_table:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexstr_entry
get_indexstr_entry:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexstr_object
get_indexstr_object:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr_0
get_indexstr_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1.1.0)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr_1
get_indexstr_1:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr_1_96
get_indexstr_1_96:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1.1.1.96)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr_1_97
get_indexstr_1_97:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1.1.1.97)" = "STRING: a" ]

REGRESS_TARGETS+=	get_indexstr_1_97_tail
get_indexstr_1_97_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1.1.1.97.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr_2_97
get_indexstr_2_97:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.13.1.1.2.97)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximplstr_table
get_indeximplstr_table:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.14)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indeximplstr_entry
get_indeximplstr_entry:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.14.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indeximplstr_object
get_indeximplstr_object:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.14.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximplstr_96
get_indeximplstr_96:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.14.1.1.96)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximplstr_97
get_indeximplstr_97:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.14.1.1.97)" = "STRING: a" ]

REGRESS_TARGETS+=	get_indeximplstr_97_97
get_indeximplstr_97_97:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.14.1.1.97.97)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximplstr_100
get_indeximplstr_100:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.14.1.1.100)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_table
get_indexstr2_table:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexstr2_entry
get_indexstr2_entry:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexstr2_object
get_indexstr2_object:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_1_97_1_97
get_indexstr2_1_97_1_97:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.97)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_1_97_1_98
get_indexstr2_1_97_1_98:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.98)" = "STRING: a - b" ]

REGRESS_TARGETS+=	get_indexstr2_1_97_1_98_tail
get_indexstr2_1_97_1_98_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.98.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_1_97_2_98
get_indexstr2_1_97_2_98:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.98)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_1_97_2_98_98
get_indexstr2_1_97_2_98_98:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.98.98)" = "STRING: a - bb" ]

REGRESS_TARGETS+=	get_indexstr2_1_97_2_98_98_tail
get_indexstr2_1_97_2_98_98_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.98.98.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_1_99_2_98_98
get_indexstr2_1_99_2_98_98:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.99.2.98.98)" = "STRING: c - bb" ]

REGRESS_TARGETS+=	get_indexstr2_1_99_2_98_98_tail
get_indexstr2_1_99_2_98_98_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.99.2.98.98.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_1_100_2_98_98
get_indexstr2_1_100_2_98_98:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.100.2.98.98)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexstr2_1_99_2_98_99
get_indexstr2_1_99_2_98_99:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.15.1.1.1.99.2.98.99)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_table
get_indeximploid_table:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indeximploid_entry
get_indeximploid_entry:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indeximploid_object
get_indeximploid_object:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_1
get_indeximploid_1:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd
get_indeximploid_openbsd:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd_100_1
get_indeximploid_openbsd_100_1:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.1)" = "OID: openBSD.100.1" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd_100_1_tail
get_indeximploid_openbsd_100_1_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd_100_1_tail
get_indeximploid_openbsd_100_1_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd_100_20_1_2
get_indeximploid_openbsd_100_20_1_2:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.20.1.2)" = "OID: openBSD.100.20.1.2" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd_100_20_1_2_tail
get_indeximploid_openbsd_100_20_1_2_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.20.1.2.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd_100_4294967294
get_indeximploid_openbsd_100_4294967294:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.4294967294)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indeximploid_openbsd_100_4294967296
get_indeximploid_openbsd_100_4294967296:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.4294967294)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexipaddress_table
get_indexipaddress_table:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexipaddress_entry
get_indexipaddress_entry:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1)" = "${NOOBJECT}" ]

REGRESS_TARGETS+=	get_indexipaddress_object
get_indexipaddress_object:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexipaddress_10
get_indexipaddress_10:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.10)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexipaddress_10_0
get_indexipaddress_10_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.10.0)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexipaddress_10_0_0
get_indexipaddress_10_0_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.10.0.0)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexipaddress_10_0_0_0
get_indexipaddress_10_0_0_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.10.0.0.0)" = "IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	get_indexipaddress_10_0_0_0_tail
get_indexipaddress_10_0_0_0_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.10.0.0.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexipaddress_192_168_0_0
get_indexipaddress_192_168_0_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.192.168.0.0)" = "IpAddress: 192.168.0.0" ]

REGRESS_TARGETS+=	get_indexipaddress_192_168_0_0_tail
get_indexipaddress_192_168_0_0_tail:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.192.168.0.0.1)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	get_indexipaddress_193_168_0_0
get_indexipaddress_193_168_0_0:
	[ "$$(snmp get -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100.17.1.1.193.168.0.0)" = "${NOINSTANCE}" ]

REGRESS_TARGETS+=	getnext_region
getnext_region:
	[ "$$(snmp getnext -v2c -cpublic -Ov 127.0.0.1:6161 openbsd.100)" = "INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_region
getnext_region:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100)" = "openBSD.100.1.0 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_scalarinteger
getnext_scalarinteger:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.1)" = "openBSD.100.1.0 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_scalarinteger_0
getnext_scalarinteger_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.1.0)" = "openBSD.100.2.0 = STRING: scalar-string" ]

REGRESS_TARGETS+=	getnext_scalarinteger_0_tail
getnext_scalarinteger_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.1.0.1)" = "openBSD.100.2.0 = STRING: scalar-string" ]

REGRESS_TARGETS+=	getnext_scalarinteger_index
getnext_scalarinteger_index:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.1.1)" = "openBSD.100.2.0 = STRING: scalar-string" ]

REGRESS_TARGETS+=	getnext_scalarstring
getnext_scalarstring:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.2)" = "openBSD.100.2.0 = STRING: scalar-string" ]

REGRESS_TARGETS+=	getnext_scalarstring_0
getnext_scalarstring_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.2.0)" = "openBSD.100.4.0 = OID: openBSD" ]

REGRESS_TARGETS+=	getnext_scalarstring_0_tail
getnext_scalarstring_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.2.0.1)" = "openBSD.100.4.0 = OID: openBSD" ]

REGRESS_TARGETS+=	getnext_scalarstring_index
getnext_scalarstring_index:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.2.1)" = "openBSD.100.4.0 = OID: openBSD" ]

REGRESS_TARGETS+=	getnext_gap
getnext_gap:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.3)" = "openBSD.100.4.0 = OID: openBSD" ]

REGRESS_TARGETS+=	getnext_scalaroid
getnext_scalaroid:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.4)" = "openBSD.100.4.0 = OID: openBSD" ]

REGRESS_TARGETS+=	getnext_scalaroid_0
getnext_scalaroid_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.4.0)" = "openBSD.100.5.0 = IpAddress: 127.0.0.1" ]

REGRESS_TARGETS+=	getnext_scalarcounter64_0
getnext_scalarcounter64_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.10.0)" = "openBSD.100.11.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint_table
getnext_indexint_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11)" = "openBSD.100.11.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint_entry
getnext_indexint_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1)" = "openBSD.100.11.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint_object
getnext_indexint_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1.1)" = "openBSD.100.11.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint_object_0
getnext_indexint_object_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1.1.0)" = "openBSD.100.11.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint_object_0_tail
getnext_indexint_object_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1.1.0.1)" = "openBSD.100.11.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint_object_1
getnext_indexint_object_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1.1.1)" = "openBSD.100.11.1.1.2 = INTEGER: 2" ]

REGRESS_TARGETS+=	getnext_indexint_object_1_tail
getnext_indexint_object_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1.1.1.1)" = "openBSD.100.11.1.1.2 = INTEGER: 2" ]

REGRESS_TARGETS+=	getnext_indexint_object_15
getnext_indexint_object_15:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1.1.15)" = "openBSD.100.12.1.1.0.0 = INTEGER: 0" ]

REGRESS_TARGETS+=	getnext_indexint_object_15_tail
getnext_indexint_object_15_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.11.1.1.15.1)" = "openBSD.100.12.1.1.0.0 = INTEGER: 0" ]

REGRESS_TARGETS+=	getnext_indexint2_table
getnext_indexint2_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12)" = "openBSD.100.12.1.1.0.0 = INTEGER: 0" ]

REGRESS_TARGETS+=	getnext_indexint2_entry
getnext_indexint2_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1)" = "openBSD.100.12.1.1.0.0 = INTEGER: 0" ]

REGRESS_TARGETS+=	getnext_indexint2_object
getnext_indexint2_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1)" = "openBSD.100.12.1.1.0.0 = INTEGER: 0" ]

REGRESS_TARGETS+=	getnext_indexint2_0
getnext_indexint2_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.0)" = "openBSD.100.12.1.1.0.0 = INTEGER: 0" ]

REGRESS_TARGETS+=	getnext_indexint2_0_0
getnext_indexint2_0_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.0.0)" = "openBSD.100.12.1.1.0.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint2_0_0_tail
getnext_indexint2_0_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.0.0.1)" = "openBSD.100.12.1.1.0.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexint2_0_1
getnext_indexint2_0_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.0.1)" = "openBSD.100.12.1.1.1.0 = INTEGER: 2" ]

REGRESS_TARGETS+=	getnext_indexint2_0_1_tail
getnext_indexint2_0_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.0.1.1)" = "openBSD.100.12.1.1.1.0 = INTEGER: 2" ]

REGRESS_TARGETS+=	getnext_indexint2_0_2
getnext_indexint2_0_2:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.0.2)" = "openBSD.100.12.1.1.1.0 = INTEGER: 2" ]

REGRESS_TARGETS+=	getnext_indexint2_0_2_tail
getnext_indexint2_0_2_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.0.2.1)" = "openBSD.100.12.1.1.1.0 = INTEGER: 2" ]

REGRESS_TARGETS+=	getnext_indexint2_1
getnext_indexint2_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.1)" = "openBSD.100.12.1.1.1.0 = INTEGER: 2" ]

REGRESS_TARGETS+=	getnext_indexint2_1_0
getnext_indexint2_1_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.1.0)" = "openBSD.100.12.1.1.1.1 = INTEGER: 3" ]

REGRESS_TARGETS+=	getnext_indexint2_1_0_tail
getnext_indexint2_1_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.1.0.1)" = "openBSD.100.12.1.1.1.1 = INTEGER: 3" ]

REGRESS_TARGETS+=	getnext_indexint2_8_1
getnext_indexint2_8_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.8.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexint2_8_1_tail
getnext_indexint2_8_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.8.1.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexint2_9
getnext_indexint2_9:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.12.1.1.9)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_table
getnext_indexstr_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_entry
getnext_indexstr_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_object
getnext_indexstr_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_object
getnext_indexstr_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_0
getnext_indexstr_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.0)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_1
getnext_indexstr_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_1_1
getnext_indexstr_1_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_1_1_tail
getnext_indexstr_1_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.1.1)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_1_96
getnext_indexstr_1_96:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.96)" = "openBSD.100.13.1.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_1_97
getnext_indexstr_1_97:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.97)" = "openBSD.100.13.1.1.1.98 = STRING: b" ]

REGRESS_TARGETS+=	getnext_indexstr_1_97_tail
getnext_indexstr_1_97_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.97.1)" = "openBSD.100.13.1.1.1.98 = STRING: b" ]

REGRESS_TARGETS+=	getnext_indexstr_1_99
getnext_indexstr_1_99:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.99)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_1_99_tail
getnext_indexstr_1_99_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.99.1)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_1_999
getnext_indexstr_1_999:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.1.999)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_2
getnext_indexstr_2:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.2)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indexstr_999
getnext_indexstr_999:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.13.1.1.999)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_table
getnext_indeximplstr_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_entry
getnext_indeximplstr_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_object
getnext_indeximplstr_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_0
getnext_indeximplstr_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.0)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_0_tail
getnext_indeximplstr_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.0.1)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_96
getnext_indeximplstr_96:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.96)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_96_tail
getnext_indeximplstr_96_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.96.1)" = "openBSD.100.14.1.1.97 = STRING: a" ]

REGRESS_TARGETS+=	getnext_indeximplstr_97
getnext_indeximplstr_97:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.97)" = "openBSD.100.14.1.1.98 = STRING: b" ]

REGRESS_TARGETS+=	getnext_indeximplstr_97_tail
getnext_indeximplstr_97_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.97.1)" = "openBSD.100.14.1.1.98 = STRING: b" ]

REGRESS_TARGETS+=	getnext_indeximplstr_97_tail
getnext_indeximplstr_97_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.97.1)" = "openBSD.100.14.1.1.98 = STRING: b" ]

REGRESS_TARGETS+=	getnext_indeximplstr_99
getnext_indeximplstr_99:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.99)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indeximplstr_99_tail
getnext_indeximplstr_99_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.99.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indeximplstr_100
getnext_indeximplstr_100:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.100)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indeximplstr_999
getnext_indeximplstr_999:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.999)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indeximplstr_999
getnext_indeximplstr_999:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.14.1.1.999)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_table
getnext_indexstr2_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_entry
getnext_indexstr2_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_object
getnext_indexstr2_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_0
getnext_indexstr2_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.0)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1
getnext_indexstr2_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_1
getnext_indexstr2_1_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_96
getnext_indexstr2_1_96:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.96)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97
getnext_indexstr2_1_97:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_0
getnext_indexstr2_1_97_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.0)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1
getnext_indexstr2_1_97_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1_1
getnext_indexstr2_1_97_1_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1_1_tail
getnext_indexstr2_1_97_1_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.1.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1_97
getnext_indexstr2_1_97_1_97:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.97)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1_97_tail
getnext_indexstr2_1_97_1_97_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.97.1)" = "openBSD.100.15.1.1.1.97.1.98 = STRING: a - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1_98
getnext_indexstr2_1_97_1_98:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.98)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1_98_1
getnext_indexstr2_1_97_1_98_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.98.1)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_1_98_1
getnext_indexstr2_1_97_1_98_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.1.98.1)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2
getnext_indexstr2_1_97_2:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2
getnext_indexstr2_1_97_2:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_1
getnext_indexstr2_1_97_2_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.1)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_1_1
getnext_indexstr2_1_97_2_1_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.1.1)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_1_1_tail
getnext_indexstr2_1_97_2_1_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.1.1.1)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_97
getnext_indexstr2_1_97_2_97:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.97)" = "openBSD.100.15.1.1.1.97.2.97.97 = STRING: a - aa" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_97_97
getnext_indexstr2_1_97_2_97_97:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.97.97)" = "openBSD.100.15.1.1.1.97.2.98.98 = STRING: a - bb" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_97_97_tail
getnext_indexstr2_1_97_2_97_97_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.97.97.1)" = "openBSD.100.15.1.1.1.97.2.98.98 = STRING: a - bb" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_97_98
getnext_indexstr2_1_97_2_97_98:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.97.98)" = "openBSD.100.15.1.1.1.97.2.98.98 = STRING: a - bb" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_98_97
getnext_indexstr2_1_97_2_98_97:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.98.97)" = "openBSD.100.15.1.1.1.97.2.98.98 = STRING: a - bb" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_2_98_97_tail
getnext_indexstr2_1_97_2_98_97_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.2.98.97.1)" = "openBSD.100.15.1.1.1.97.2.98.98 = STRING: a - bb" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_3
getnext_indexstr2_1_97_3:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.3)" = "openBSD.100.15.1.1.1.98.1.98 = STRING: b - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_3
getnext_indexstr2_1_97_3:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.3)" = "openBSD.100.15.1.1.1.98.1.98 = STRING: b - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_97_3
getnext_indexstr2_1_97_3:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.97.3)" = "openBSD.100.15.1.1.1.98.1.98 = STRING: b - b" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_99_2_98_98
getnext_indexstr2_1_99_2_98_98:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.99.2.98.98)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexstr2_1_99_2_98_98
getnext_indexstr2_1_99_2_98_98:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.15.1.1.1.99.2.98.98)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_table
getnext_indexoid_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_entry
getnext_indexoid_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_object
getnext_indexoid_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_0
getnext_indexoid_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.0)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_1
getnext_indexoid_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_1_3_6_1_4_1_30155_100_0
getnext_indexoid_1_3_6_1_4_1_30155_100_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.0)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_1_3_6_1_4_1_30155_100_0_tail
getnext_indexoid_1_3_6_1_4_1_30155_100_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.0.1)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.1 = OID: openBSD.100.1" ]

REGRESS_TARGETS+=	getnext_indexoid_1_3_6_1_4_1_30155_100_1
getnext_indexoid_1_3_6_1_4_1_30155_100_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.1)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.2 = OID: openBSD.100.2" ]

REGRESS_TARGETS+=	getnext_indexoid_1_3_6_1_4_1_30155_100_1_tail
getnext_indexoid_1_3_6_1_4_1_30155_100_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.1.1)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.2 = OID: openBSD.100.2" ]

REGRESS_TARGETS+=	getnext_indexoid_1_3_6_1_4_1_30155_100_4294967294
getnext_indexoid_1_3_6_1_4_1_30155_100_4294967294:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.4294967294)" = "openBSD.100.16.1.1.1.3.6.1.4.1.30155.100.4294967295 = OID: openBSD.100.4294967295" ]

REGRESS_TARGETS+=	getnext_indexoid_1_3_6_1_4_1_30155_100_4294967295
getnext_indexoid_1_3_6_1_4_1_30155_100_4294967295:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.4294967295)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexoid_1_3_6_1_4_1_30155_100_4294967295_tail
getnext_indexoid_1_3_6_1_4_1_30155_100_4294967295_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.16.1.1.1.3.6.1.4.1.30155.100.4294967295.1)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_table
getnext_indexipaddress_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_entry
getnext_indexipaddress_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_object
getnext_indexipaddress_object:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_0
getnext_indexipaddress_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.0)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_10
getnext_indexipaddress_10:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.10)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_10_0
getnext_indexipaddress_10_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.10.0)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_10_0_0
getnext_indexipaddress_10_0_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.10.0.0)" = "openBSD.100.17.1.1.10.0.0.0 = IpAddress: 10.0.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_10_0_0_0
getnext_indexipaddress_10_0_0_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.10.0.0.0)" = "openBSD.100.17.1.1.127.0.0.1 = IpAddress: 127.0.0.1" ]

REGRESS_TARGETS+=	getnext_indexipaddress_10_0_0_1
getnext_indexipaddress_10_0_0_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.10.0.0.1)" = "openBSD.100.17.1.1.127.0.0.1 = IpAddress: 127.0.0.1" ]

REGRESS_TARGETS+=	getnext_indexipaddress_10_0_1
getnext_indexipaddress_10_0_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.10.0.1)" = "openBSD.100.17.1.1.127.0.0.1 = IpAddress: 127.0.0.1" ]

REGRESS_TARGETS+=	getnext_indexipaddress_10_1
getnext_indexipaddress_10_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.10.1)" = "openBSD.100.17.1.1.127.0.0.1 = IpAddress: 127.0.0.1" ]

REGRESS_TARGETS+=	getnext_indexipaddress_192_168
getnext_indexipaddress_192_168:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.192.168)" = "openBSD.100.17.1.1.192.168.0.0 = IpAddress: 192.168.0.0" ]

REGRESS_TARGETS+=	getnext_indexipaddress_192_168_0_0
getnext_indexipaddress_192_168_0_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.17.1.1.192.168.0.0)" = "openBSD.100.18.1.1.5 = INTEGER: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_table
getnext_indexintvalue_table:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18)" = "openBSD.100.18.1.1.5 = INTEGER: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_entry
getnext_indexintvalue_entry:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1)" = "openBSD.100.18.1.1.5 = INTEGER: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_int
getnext_indexintvalue_int:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1.1)" = "openBSD.100.18.1.1.5 = INTEGER: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_int_0
getnext_indexintvalue_int_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1.1.0)" = "openBSD.100.18.1.1.5 = INTEGER: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_int_5
getnext_indexintvalue_int_5:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1.1.5)" = "openBSD.100.18.1.2.5 = STRING: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_int_5_tail
getnext_indexintvalue_int_5_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1.1.5.1)" = "openBSD.100.18.1.2.5 = STRING: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_int_6
getnext_indexintvalue_int_6:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1.1.6)" = "openBSD.100.18.1.2.5 = STRING: 5" ]

REGRESS_TARGETS+=	getnext_indexintvalue_string_5
getnext_indexintvalue_string_5:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1.2.5)" = "openBSD.100.19.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintvalue_string_5
getnext_indexintvalue_string_5:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.18.1.2.5)" = "openBSD.100.19.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintany_int
getnext_indexintany_int:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.19.1.1)" = "openBSD.100.19.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintany_int_0
getnext_indexintany_int_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.19.1.1.0)" = "openBSD.100.19.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintany_int_0_tail
getnext_indexintany_int_0_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.19.1.1.0.1)" = "openBSD.100.19.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintany_int_1
getnext_indexintany_int_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.19.1.1.1)" = "openBSD.100.19.1.2.1 = STRING: 1" ]

REGRESS_TARGETS+=	getnext_indexintany_int_1_tail
getnext_indexintany_int_1_tail:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.19.1.1.1.1)" = "openBSD.100.19.1.2.1 = STRING: 1" ]

REGRESS_TARGETS+=	getnext_indexintany_string_1
getnext_indexintany_string_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.19.1.2.1)" = "openBSD.100.20.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintnew_int
getnext_indexintnew_int:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.20.1.1)" = "openBSD.100.20.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintnew_int_0
getnext_indexintnew_int_0:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.20.1.1.0)" = "openBSD.100.20.1.1.1 = INTEGER: 1" ]

REGRESS_TARGETS+=	getnext_indexintnew_int_1
getnext_indexintnew_int_1:
	[ "$$(snmp getnext -v2c -cpublic 127.0.0.1:6161 openbsd.100.20.1.1.1)" = "openBSD.100.20.1.2.1 = STRING: 1" ]

.include <bsd.regress.mk>
