#
# $FreeBSD$
#

MOD=	bridge
SRCS=	bridge_snmp.c bridge_if.c bridge_port.c bridge_addrs.c \
	bridge_pf.c bridge_sys.c

XSYM=	dot1dBridge newRoot topologyChange begemotBridgeNewRoot \
	begemotBridgeTopologyChange begemotBridgeBaseName

MAN=	snmp_bridge.3

BMIBS=	BRIDGE-MIB.txt BEGEMOT-BRIDGE-MIB.txt RSTP-MIB.txt
DEFS=	${MOD}_tree.def
INCS=	${MOD}_snmp.h

.include <bsd.snmpmod.mk>
