#	$OpenBSD: Makefile,v 1.217 2025/05/07 03:02:55 dlg Exp $

.include <bsd.own.mk>

SUBDIR=	ac accton acme-client acpidump adduser amd apm apmd arp authpf \
	bgpctl bgpd bgplgd bpflogd btrace chroot config cron crunchgen \
	dev_mkdb dhcp6leasectl dhcpd dhcpleasectl dhcrelay dhcrelay6 \
	dvmrpctl dvmrpd \
	edquota eeprom eigrpd eigrpctl fdformat ftp-proxy fw_update gpioctl \
	hostapd hostctl hotplugd httpd identd ifstated ikectl inetd \
	installboot iostat iscsictl iscsid kgmon kvm_mkdb ldapd ldapctl \
	ldomctl ldomd ldpd ldpctl lldp lldpd lpr \
	makefs mailwrapper map-mbone memconfig \
	mksuncd mkuboot mopd mrinfo mrouted mtrace mtree ndp netgroup_mkdb \
	npppctl npppd nsd ntpd ocspcheck ospfctl ospfd ospf6d ospf6ctl \
	pcidump pkg_add portmap pppd procmap pstat pwd_mkdb \
	quot quotaon ractl rad radiusctl radiusd rarpd rbootd \
	rcctl rdate rdsetroot relayctl relayd repquota ripctl ripd \
	rmt route6d rpc.bootparamd rpc.lockd rpc.statd rpki-client \
	sa sasyncd sensorsd slaacctl slowcgi smtpd \
	snmpd spamdb syslogc syslogd sysmerge syspatch sysupgrade \
	tcpdrop tcpdump tftp-proxy tftpd tokenadm tokeninit traceroute \
	trpt unbound unwindctl usbdevs user vmd vmctl vipw \
	watchdogd wsconscfg wsfontload wsmoused zdump zic

.if (${YP:L} == "yes")
SUBDIR+=ypbind ypldap yppoll ypset ypserv
.endif

.include <bsd.subdir.mk>
