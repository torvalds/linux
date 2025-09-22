#	$OpenBSD: Makefile,v 1.33 2022/09/17 09:30:18 kn Exp $

SUBDIR += acme-client
SUBDIR += arp
SUBDIR += bgpd
SUBDIR += btrace
SUBDIR += httpd
SUBDIR += ifstated
SUBDIR += installboot
SUBDIR += ldapd
SUBDIR += mtree
SUBDIR += ocspcheck
SUBDIR += ospfd
SUBDIR += ospf6d
SUBDIR += relayd
SUBDIR += rpki-client
SUBDIR += snmpd
SUBDIR += syslogd

.if ${MACHINE} == "amd64"
SUBDIR += vmd
.endif

.if defined(REGRESS_FULL) || make(clean) || make(cleandir) || make(obj)
SUBDIR += pkg_add
.endif

.include <bsd.subdir.mk>
