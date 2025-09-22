# $OpenBSD: Makefile,v 1.10 2022/04/29 17:27:38 bluhm Exp $
#
#  template_v10
#  ----------------
#  * create pflow0 sending from 127.0.0.1 to 127.0.0.1:9996
#  * wait for first template and compare it to known good template
#  * destroy pflow0
#
#  flow_10_{4,6}
#  -----------------
#  * create pflow0 sending from 127.0.0.1 to 127.0.0.1:9996
#  * add 10.11.12.13 and 2001:db8::13 to lo0
#  * load pf ruleset which skips on all interfaces except lo0 to not lose
#    existing ssh sessions, e.g.:
#	set skip on {em0 cas0 cas1 enc0}
#	pass on lo0 no state
#	pass on lo0 proto tcp from port 12345 to port 12346 keep state (pflow)
#  * enable pf
#  * generate IPv4 or IPv6 traffic
#  * wait for flow and compare it with known good flow
#  * destroy pflow0
#  * delete 10.11.12.13 and 2001:db8::13 from lo0
#
# ifconfig
# --------
# Take up pflow0 and test various combinations of flowdst, flowsrc and proto.
# Destroy it in the end.

REGRESS_TARGETS=template_v10 flow_10_4 flow_10_6 ifconfig
REGRESS_ROOT_TARGETS=${REGRESS_TARGETS}

PROG=	gen_traffic
LDADD=	-levent

.if ! (make(clean) || make(cleandir) || make(obj))

PF_STATUS !=	${SUDO} /sbin/pfctl -si | sed -n 's/^Status: \([^ ]*\) .*/\1/p'
.if empty(PF_STATUS:MEnabled)
regress:
	@echo pf status: "${PF_STATUS}"
	@echo Enable pf to run this regress.
	@echo SKIPPED
.endif

PERL_REQUIRE !=	perl -e 'eval { require Net::Flow } or print $@'
.if ! empty(PERL_REQUIRE)
regress:
	@echo "${PERL_REQUIRE}"
	@echo Install the p5-Net-Flow package to run pflow test.
	@echo SKIPPED
.endif

.endif

template_v10:
	${SUDO} perl ${.CURDIR}/template.pl 10 \
	    | diff -up ${.CURDIR}/template.v10 /dev/stdin

flow_10_4: gen_traffic
	${SUDO} perl ${.CURDIR}/flow.pl 10 4 \
	    | diff -up ${.CURDIR}/flow.10_4 /dev/stdin
flow_10_6: gen_traffic
	${SUDO} perl ${.CURDIR}/flow.pl 10 6 \
	    | diff -up ${.CURDIR}/flow.10_6 /dev/stdin

ifconfig:
	${SUDO} sh ${.CURDIR}/ifconfig.sh 2>&1 \
	    | diff -up ${.CURDIR}/ifconfig.ok /dev/stdin

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
