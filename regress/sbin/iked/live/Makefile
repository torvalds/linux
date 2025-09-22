#	$OpenBSD: Makefile,v 1.34 2021/12/21 13:50:35 tobhe Exp $

# Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

REGRESS_SETUP_ONCE =	setup
REGRESS_CLEANUP =	cleanup
CLEANFILES =		*.conf *.cnf *.csr *.key *.crt *.srl

LEFT_SSH ?=
RIGHT_SSH ?=
LEFT_ADDR ?=
RIGHT_ADDR ?=

.if empty(LEFT_SSH) || empty(RIGHT_SSH) || empty(LEFT_ADDR) || empty(RIGHT_ADDR)
regress:
	@echo this test needs two remote machines to operate
	@echo LEFT_SSH RIGHT_SSH RIGHT_ADDR LEFT_ADDR are not defined
	@echo SKIPPED
.endif

TEST_FLOWS = \
	[ -z $$tmode ] && tmode=tunnel; \
	_ret=1; \
	count=0; \
	dynamic=${RIGHT_ADDR}; \
	if [ -n "$$config_address" ]; then \
		dynamic="172.16.13.[0-9]+"; \
	fi; \
	[ -z "$$maxwait" ] && maxwait=3; \
	while [[ $$count -le $$maxwait ]]; do \
		ipsecctlleft=`ssh ${LEFT_SSH} ipsecctl -sa`; \
		ipsecctlright=`ssh ${RIGHT_SSH} ipsecctl -sa`; \
		flowleft=`echo "$$ipsecctlleft" \
		    | sed -E -n "/^flow $$flowtype in from $$dynamic\
		    to ${LEFT_ADDR} peer ${RIGHT_ADDR} srcid (FQDN|UFQDN|ASN1_DN)\/[^ ]*\
		    dstid (FQDN|UFQDN|ASN1_DN)\/[^ ]*/p"`; \
		flowright=`echo "$$ipsecctlright" \
		    | sed -E -n "/^flow $$flowtype in from ${LEFT_ADDR}\
		    to $$dynamic peer ${LEFT_ADDR} srcid (FQDN|UFQDN|ASN1_DN)\/[^ ]*\
		    dstid (FQDN|UFQDN|ASN1_DN)\/[^ ]*/p"`; \
		saleft_rtol=`echo "$$ipsecctlleft" \
		    | sed -n "/^$$flowtype $$tmode from ${RIGHT_ADDR} to ${LEFT_ADDR}/p"`; \
		saleft_ltor=`echo "$$ipsecctlleft" \
		    | sed -n "/^$$flowtype $$tmode from ${LEFT_ADDR} to ${RIGHT_ADDR}/p"`; \
		saright_rtol=`echo "$$ipsecctlright" \
		    | sed -n "/^$$flowtype $$tmode from ${RIGHT_ADDR} to ${LEFT_ADDR}/p"`; \
		saright_ltor=`echo "$$ipsecctlright" \
		    | sed -n "/^$$flowtype $$tmode from ${LEFT_ADDR} to ${RIGHT_ADDR}/p"`; \
		if [[ -n "$$saleft_ltor" && -n "$$saleft_rtol" && \
		     -n "$$saright_ltor" && -n "$$saright_rtol" && \
		     -n "$$flowleft" && -n "$$flowright" ]]; then \
			 _ret=0; \
			 break; \
		fi; \
		let count=$$count+1; \
	done; \
	if [[ "$${_ret}" -ne 0 ]]; then \
		echo "SAs not found:\n$$ipsecctlleft\n$$ipsecctlright"; \
	fi

TEST_PING = \
	_ret=1; \
	if [[ "${IPV}" == "6" ]]; then ping="ping6"; else ping="ping"; fi; \
	dump=`ssh ${LEFT_SSH} "tcpdump -n -c2 -i enc0 -w /tmp/test.pcap > /dev/null & \
	    $$ping -w 1 -n -c 5 ${RIGHT_ADDR} > /dev/null && \
	    tcpdump -n -r /tmp/test.pcap && rm -f /tmp/test.pcap; \
	    kill -9 \\$$! > /dev/null 2>&1 || true"`; \
	rtol=`echo "$$dump" \
	    | sed -n "/(authentic,confidential): SPI 0x[0-9a-f]\{8\}: ${LEFT_ADDR} > ${RIGHT_ADDR}/p"`; \
	ltor=`echo "$$dump" \
	    | sed -n "/(authentic,confidential): SPI 0x[0-9a-f]\{8\}: ${RIGHT_ADDR} > ${LEFT_ADDR}/p"`; \
	if [[ -z "$$rtol" || -z "$$ltor" ]]; then \
		_ret=1; \
	else \
		_ret=0; \
	fi; \
	echo "$$dump"

TEST_SINGLEIKESA = \
	count=`ssh ${LEFT_SSH} "ikectl show sa | grep -c iked_sas"`; \
	if [[ "$$count" != "1" ]]; then \
		echo "error: too many IKE SAs."; \
		exit 1; \
	fi

SETUP_CONFIG = \
	from=$$local; \
	to=$$peer; \
	if [[ -z "$$mode" ]]; then mode="active"; fi; \
	authstr=""; \
	if [[ "$$auth" = "psk" ]]; then \
		authstr="psk $$psk"; \
	fi; \
	ipcomp=""; \
	if [[ "$$flowtype" = "ipcomp" ]]; then \
		ipcomp="ipcomp"; \
	fi; \
	global=""; \
	if [ "$$fragmentation" = true ]; then \
		global="$${global}set fragmentation\n"; \
	fi; \
	if [ "$$singleikesa" = true ]; then \
		global="$${global}set enforcesingleikesa\n"; \
	fi; \
	if [ "$$intermediate" = true ]; then \
		global="$${global}set cert_partial_chain\n"; \
	fi; \
	confstr=""; \
	if [ -n "$$config_address" ]; then \
		if [ "$$side" = left ]; then \
			mode=passive; \
			confstr="config address $$config_address"; \
			if [[ "$$config_address" == */* ]]; then \
				to="dynamic"; \
			else \
				to="$$config_address"; \
			fi; \
		else \
			mode=active; \
			confstr="request address any"; \
			if [[ "$$config_address" == */* ]]; then \
				from="dynamic"; \
			else \
				from="$$config_address"; \
			fi; \
		fi; \
	fi; \
	echo "MODE=\"$$mode\"" >> $@_$$side.conf; \
	echo "TMODE=\"$$tmode\"" >> $@_$$side.conf; \
	echo "FROM=\"$$from\"" >> $@_$$side.conf; \
	echo "TO=\"$$to\"" >> $@_$$side.conf; \
	echo "LOCAL_ADDR=\"$$local\"" >> $@_$$side.conf; \
	echo "PEER_ADDR=\"$$peer\"" >> $@_$$side.conf; \
	echo "IPCOMP=\"$$ipcomp\"" >> $@_$$side.conf; \
	echo "SRCID=\"\\\"$$srcid\\\"\"" >> $@_$$side.conf; \
	echo "DSTID=\"$$dstid\"" >> $@_$$side.conf; \
	echo "AUTH=\"$$authstr\"" >> $@_$$side.conf; \
	echo "CONFIG=\"$$confstr\"" >> $@_$$side.conf; \
	echo "IKESA=\"$$ikesa\"" >> $@_$$side.conf; \
	echo "$$global" >> $@_$$side.conf; \
	cat ${.CURDIR}/iked.in >> $@_$$side.conf

DEPLOY_CONFIGS = \
	chmod 0600 $@_left.conf; \
	echo "cd /tmp\nput $@_left.conf test.conf" | sftp -q ${LEFT_SSH}; \
	chmod 0600 $@_right.conf; \
	echo "cd /tmp\nput $@_right.conf test.conf" | sftp -q ${RIGHT_SSH}; \
	rm -f $@_left.conf $@_right.conf

SETUP_CONFIGS = \
	if [[ "$$auth" = "psk" ]]; then \
		psk=`openssl rand -hex 20`; \
	fi; \
	side=left; \
	srcid=$$leftid; \
	local=${LEFT_ADDR}; \
	peer=${RIGHT_ADDR}; \
	    ${SETUP_CONFIG}; \
	side=right; \
	srcid=$$rightid; \
	local=${RIGHT_ADDR}; \
	peer=${LEFT_ADDR}; \
	    ${SETUP_CONFIG}; \
	${DEPLOY_CONFIGS}

SETUP_SYSCTL = \
	ssh ${LEFT_SSH} "sysctl $$sysctl"; \
	ssh ${RIGHT_SSH} "sysctl $$sysctl"

SETUP_START = \
	ssh ${LEFT_SSH} "ipsecctl -F; pkill iked; iked $$iked_flags -f /tmp/test.conf"; \
	ssh ${RIGHT_SSH} "ipsecctl -F; pkill iked; iked $$iked_flags -f /tmp/test.conf"

SETUP_RELOAD_RIGHT = \
	ssh ${RIGHT_SSH} "ikectl reload"

SETUP_CERT = \
	echo "ALTNAME = $$name-from-$$caname" > $$name-from-$$caname.cnf; \
	cat ${.CURDIR}/crt.in >> $$name-from-$$caname.cnf; \
	openssl req -config $$name-from-$$caname.cnf -new -key $$name.key -nodes \
	    -out $$name-from-$$caname.csr; \
	openssl x509 -extfile $$name-from-$$caname.cnf -extensions req_cert_extensions \
	     -req -in $$name-from-$$caname.csr -CA $$caname.crt -CAkey $$caname.key \
	     -CAcreateserial -out $$name-from-$$caname.crt

SETUP_INTERMEDIATE = \
	echo "ALTNAME = $$name-from-$$caname" > $$name-from-$$caname.cnf; \
	cat ${.CURDIR}/crt.in >> $$name-from-$$caname.cnf; \
	openssl genrsa -out $$name-from-$$caname.key 2048; \
	openssl req -config $$name-from-$$caname.cnf -new -key $$name-from-$$caname.key -nodes \
	    -out $$name-from-$$caname.csr; \
	openssl x509 -extfile $$name-from-$$caname.cnf -extensions v3_intermediate_ca \
	    -req -in $$name-from-$$caname.csr -CA $$caname.crt -CAkey $$caname.key \
	    -CAcreateserial -out $$name-from-$$caname.crt

SETUP_CA = \
	openssl genrsa -out $$caname.key 2048; \
	openssl req -subj "/C=DE/ST=Bavaria/L=Munich/O=iked/CN=$$caname" \
	     -new -x509 -key $$caname.key -out $$caname.crt

cleanup:
	-ssh ${LEFT_SSH} 'rm -f /tmp/test.conf; ipsecctl -F; pkill iked; \
	    rm -f /etc/iked/ca/*; rm -f /etc/iked/certs/*; rm -f /etc/iked/private/*; \
	    sysctl "net.inet.esp.udpencap_port=4500"; \
	    rm -f /tmp/pf.conf; pfctl -d; pfctl -f /etc/pf.conf;'
	-ssh ${RIGHT_SSH} 'rm -f /tmp/test.conf; ipsecctl -F; pkill iked; \
	    rm -f /etc/iked/ca/*; rm -f /etc/iked/certs/*; rm -f /etc/iked/private/*; \
	    sysctl "net.inet.esp.udpencap_port=4500"; \
	    rm -f /tmp/pf.conf; pfctl -d; pfctl -f /etc/pf.conf;'

setup_certs: ca-both.crt left-from-ca-both.crt left.key right-from-ca-both.crt \
    right.key ca-left.crt right-from-ca-left.crt ca-right.crt left-from-ca-right.crt \
    ca-none.crt left-from-ca-none.crt right-from-ca-none.crt \
    intermediate-from-ca-none.crt left-from-intermediate-from-ca-none.crt \
    right-from-intermediate-from-ca-none.crt
	echo "cd /etc/iked\n \
	    put left-from-ca-both.crt certs\n \
	    put left-from-ca-right.crt certs\n \
	    put left-from-ca-none.crt certs\n \
	    put left-from-intermediate-from-ca-none.crt certs\n \
	    put right-from-ca-none.crt certs\n \
	    put left.key private/local.key\n \
	    put intermediate-from-ca-none.crt ca\n \
	    put ca-left.crt ca\n \
	    put ca-both.crt ca\n" | sftp ${LEFT_SSH} -q; \
	echo "cd /etc/iked\n \
	    put right-from-ca-both.crt certs\n \
	    put right-from-ca-left.crt certs\n \
	    put right-from-ca-none.crt certs\n \
	    put right-from-intermediate-from-ca-none.crt certs\n \
	    put left-from-ca-none.crt certs\n \
	    put right.key private/local.key\n \
	    put intermediate-from-ca-none.crt ca\n \
	    put ca-right.crt ca\n \
	    put ca-both.crt ca\n" | sftp ${RIGHT_SSH} -q; \
	ssh ${LEFT_SSH} "openssl rsa -in /etc/iked/private/local.key -pubout > /etc/iked/local.pub"; \
	ssh ${RIGHT_SSH} "openssl rsa -in /etc/iked/private/local.key -pubout > /etc/iked/local.pub"

setup_pf: pf.in
	echo "cd /tmp\nput ${.CURDIR}/pf.in pf.conf" | sftp -q ${LEFT_SSH}
	echo "cd /tmp\nput ${.CURDIR}/pf.in pf.conf" | sftp -q ${RIGHT_SSH}
	-ssh ${LEFT_SSH} "pfctl -f /tmp/pf.conf; pfctl -e"
	-ssh ${RIGHT_SSH} "pfctl -f /tmp/pf.conf; pfctl -e"

setup: setup_pf setup_certs

.PHONY: setup_certs

test_flows:
	${TEST_FLOWS}

left.key right.key:
	openssl genrsa -out $@ 2048

ca-both.crt ca-both.key:
	caname=ca-both; ${SETUP_CA}

left-from-ca-both.crt: ca-both.crt ca-both.key left.key
	caname=ca-both; name=left; ${SETUP_CERT}

right-from-ca-both.crt: ca-both.crt ca-both.key right.key
	caname=ca-both; name=right; ${SETUP_CERT}

ca-left.crt ca-left.key:
	caname=ca-left; ${SETUP_CA}

right-from-ca-left.crt right.key: ca-left.crt ca-left.key
	caname=ca-left; name=right; ${SETUP_CERT}

ca-right.crt ca-right.key:
	caname=ca-right; ${SETUP_CA}

left-from-ca-right.crt left.key: ca-right.crt ca-right.key
	caname=ca-right; name=left; ${SETUP_CERT}

ca-none.crt ca-none.key:
	caname=ca-none; ${SETUP_CA}

left-from-ca-none.crt left.key: ca-none.crt ca-none.key
	caname=ca-none; name=left; ${SETUP_CERT}

right-from-ca-none.crt right.key: ca-none.crt ca-none.key
	caname=ca-none; name=right; ${SETUP_CERT}

intermediate-from-ca-none.crt intermediate-from-ca-none.key:
	caname=ca-none name=intermediate; ${SETUP_INTERMEDIATE}

left-from-intermediate-from-ca-none.crt left.key: \
     intermediate-from-ca-none.crt intermediate-from-ca-none.key
	caname=intermediate-from-ca-none; name=left; ${SETUP_CERT}

right-from-intermediate-from-ca-none.crt right.key: \
     intermediate-from-ca-none.crt intermediate-from-ca-none.key
	caname=intermediate-from-ca-none; name=right; ${SETUP_CERT}

REGRESS_TARGETS = run-ping-fail
run-ping-fail:
	ssh ${LEFT_SSH} "ipsecctl -F; pkill iked || true"
	ssh ${RIGHT_SSH} "ipsecctl -F; pkill iked || true"
	${TEST_PING}; \
	if [[ $$_ret -ne 1 ]]; then exit 1; fi

REGRESS_TARGETS += run-cert-single-ca
run-cert-single-ca:
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-cert-single-ca-asn1dn
run-cert-single-ca-asn1dn:
	leftid="/C=DE/ST=Bavaria/L=Munich/O=iked/CN=left-from-ca-both"; \
	rightid="/C=DE/ST=Bavaria/L=Munich/O=iked/CN=right-from-ca-both"; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-cert-no-ca
run-cert-no-ca:
	leftid=left-from-ca-none; \
	rightid=right-from-ca-none; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-config-address
run-config-address:
	flowtype=esp; \
	config_address=172.16.13.36; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	config_address=172.16.13.36; \
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-config-address-pool
run-config-address-pool:
	flowtype=esp; \
	config_address=172.16.13.36/31; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	config_address=172.16.13.36/31; \
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-dstid-fail
run-dstid-fail:
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    side=left; \
	    srcid=$$leftid; \
	    local=${LEFT_ADDR}; \
	    peer=${RIGHT_ADDR}; \
	    ${SETUP_CONFIG}; \
	    side=right; \
	    mode=passive; \
	    srcid=$$rightid; \
	    local=${RIGHT_ADDR}; \
	    peer=${LEFT_ADDR}; \
	    dstid="dstid invalid"; \
	    ${SETUP_CONFIG}; \
	    ${DEPLOY_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 1 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 1 ]]; then exit 1; fi

REGRESS_TARGETS += run-dstid
run-dstid:
	flowtype=esp; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    side=left; \
	    srcid=$$leftid; \
	    local=${LEFT_ADDR}; \
	    peer=${RIGHT_ADDR}; \
	    dstid="dstid $$rightid"; \
	    ${SETUP_CONFIG}; \
	    side=right; \
	    srcid=$$rightid; \
	    local=${RIGHT_ADDR}; \
	    peer=${LEFT_ADDR}; \
	    dstid="dstid $$leftid"; \
	    ${SETUP_CONFIG}; \
	    ${DEPLOY_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-dstid-multi
run-dstid-multi:
	flowtype=esp; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    side=left; srcid=$$leftid; local=${LEFT_ADDR}; peer=${RIGHT_ADDR}; \
	    dstid="dstid $$rightid"; \
	    ${SETUP_CONFIG}; \
	    side=right; mode=passive; srcid=$$rightid; local=${RIGHT_ADDR}; \
	    peer=${LEFT_ADDR}; dstid="dstid $$leftid"; \
	    ${SETUP_CONFIG}; \
	    dstid="dstid roflol"; \
	    ${SETUP_CONFIG}; \
	    ${DEPLOY_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-srcid-multi
run-srcid-multi:
	flowtype=esp; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    side=left; srcid=$$leftid; local=${LEFT_ADDR}; peer=${RIGHT_ADDR}; \
	    dstid="dstid $$rightid"; \
	    ${SETUP_CONFIG}; \
	    side=right; mode=passive; srcid="borked"; local=${RIGHT_ADDR}; \
	    peer=${LEFT_ADDR}; dstid=""; \
	    ${SETUP_CONFIG}; \
	    srcid=$$rightid; \
	    ${SETUP_CONFIG}; \
	    srcid="roflol"; \
	    ${SETUP_CONFIG}; \
	    ${DEPLOY_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-cert-multi-ca
run-cert-multi-ca:
	flowtype=esp; \
	leftid=left-from-ca-right; \
	rightid=right-from-ca-left; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-cert-second-altname
run-cert-second-altname:
	flowtype=esp; \
	leftid=left-from-ca-both-alternative; \
	rightid=right-from-ca-both@openbsd.org; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-invalid-ke
run-invalid-ke:
	flowtype=esp; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    side=left; srcid=$$leftid; local=${LEFT_ADDR}; peer=${RIGHT_ADDR}; \
	    dstid="dstid $$rightid"; \
	    ikesa="ikesa group ecp256 group curve25519"; \
	    ${SETUP_CONFIG}; \
	    side=right; mode=passive; srcid=$$rightid; local=${RIGHT_ADDR}; \
	    peer=${LEFT_ADDR}; dstid="dstid $$leftid"; \
	    ikesa="ikesa group curve25519"; \
	    ${SETUP_CONFIG}; \
	    ${DEPLOY_CONFIGS}
	${SETUP_START}
	flowtype=esp; maxwait=6; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-psk-fail
run-psk-fail:
	auth=psk; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	flowtype=esp; \
	    side=left; \
	    srcid=$$leftid; \
	    local=${LEFT_ADDR}; \
	    peer=${RIGHT_ADDR}; \
	    dstid="dstid $$rightid"; \
	    psk=`openssl rand -hex 20`; \
	    ${SETUP_CONFIG}; \
	    side=right; \
	    srcid=$$rightid; \
	    local=${RIGHT_ADDR}; \
	    peer=${LEFT_ADDR}; \
	    dstid="dstid $$leftid"; \
	    psk=`openssl rand -hex 20`; \
	    ${SETUP_CONFIG}; \
	    ${DEPLOY_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 1 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 1 ]]; then exit 1; fi

REGRESS_TARGETS += run-psk
run-psk:
	auth=psk; \
	leftid=left; \
	rightid=right; \
	flowtype=esp; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; \
	if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; \
	if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-intermediate-fail
run-intermediate-fail:
	leftid=left-from-intermediate-from-ca-none; \
	rightid=right-from-intermediate-from-ca-none; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 1 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 1 ]]; then exit 1; fi

REGRESS_TARGETS += run-intermediate
run-intermediate:
	intermediate=true; \
	leftid=left-from-intermediate-from-ca-none; \
	rightid=right-from-intermediate-from-ca-none; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-fragmentation
run-fragmentation:
	flowtype=esp; \
	fragmentation=true; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	flowtype=esp; ${TEST_FLOWS}; \
	if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; \
	if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-transport
run-transport:
	flowtype=esp; \
	tmode=transport; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	tmode=transport; flowtype=esp; \
	    ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-singleikesa
run-singleikesa:
	flowtype=esp; \
	singleikesa=true; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}
	${SETUP_START}
	sleep 1; ${SETUP_RELOAD_RIGHT}; \
	sleep 3; ${TEST_SINGLEIKESA}

REGRESS_TARGETS += run-ipcomp
run-ipcomp:
	flowtype=ipcomp; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}
	sysctl="net.inet.ipcomp.enable=1"; \
	    ${SETUP_SYSCTL}
	${SETUP_START}
	flowtype=ipcomp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi

REGRESS_TARGETS += run-udpencap-port
run-udpencap-port:
	flowtype=esp; \
	leftid=left-from-ca-both; \
	rightid=right-from-ca-both; \
	    ${SETUP_CONFIGS}; \
	sysctl="net.inet.esp.udpencap_port=9999"; \
	    ${SETUP_SYSCTL};
	iked_flags=-p9999; \
	    ${SETUP_START};
	flowtype=esp; ${TEST_FLOWS}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	${TEST_PING}; if [[ $$_ret -ne 0 ]]; then exit 1; fi
	sysctl="net.inet.esp.udpencap_port=4500"; \
	    ${SETUP_SYSCTL};

.include <bsd.regress.mk>
