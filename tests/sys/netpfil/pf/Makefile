# $FreeBSD$

PACKAGE=	tests

TESTSDIR=       ${TESTSBASE}/sys/netpfil/pf
TESTS_SUBDIRS+=	ioctl

ATF_TESTS_SH+=	anchor \
		pass_block \
		forward \
		fragmentation \
		names \
		nat \
		set_tos \
		rdr \
		route_to \
		synproxy \
		set_skip \
		pfsync	\
		icmp

${PACKAGE}FILES+=	utils.subr \
			echo_inetd.conf \
			sniffer.py \
			pft_ping.py \
			CVE-2019-5597.py \
			CVE-2019-5598.py

${PACKAGE}FILESMODE_pft_ping.py=	0555
${PACKAGE}FILESMODE_CVE-2019-5597.py=	0555
${PACKAGE}FILESMODE_CVE-2019-5598.py=	0555

.include <bsd.test.mk>
