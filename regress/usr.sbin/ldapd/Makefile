# $OpenBSD: Makefile,v 1.12 2022/05/20 09:46:47 bluhm Exp $

OUT=${.CURDIR}/out
DIT=dc=example,dc=com
DIT2=dc=bar,dc=quux
NLV=0.65
CLEANFILES=ldapd.log ldapd1.conf ${DIT}_indx.db ${DIT}_data.db ${DIT2}_indx.db ${DIT2}_data.db

# Needs p5-ldap and openldap (client)

PERL_REQUIRE !=	perl -Mstrict -Mwarnings -e ' \
    eval { require Net::LDAP } or print $@; \
'
.if ! empty (PERL_REQUIRE)
regress:
	@echo "${PERL_REQUIRE}"
	@echo install package p5-ldap
	@echo SKIPPED
.endif

.if ! (make(clean) || make(cleandir) || make(obj))
LDAP_DEP != ldapsearch -VV 2>/dev/null ; echo $?
.endif
.if ("${LDAP_DEP}" != 0)
regress:
	@echo openldap-client must be installed
	@echo SKIPPED
.endif

REGRESS_TARGETS = connect import perl purge
REGRESS_ROOT_TARGETS = ${REGRESS_TARGETS}
REGRESS_SETUP_ONCE = bootstrap
REGRESS_CLEANUP = kill

bootstrap: kill
	[ -z "${SUDO}" ] || ${SUDO} true
	rm -f ldapd1.conf ldapd.log
	${SUDO} install -m 600 -o root ${.CURDIR}/ldapd.conf ldapd1.conf
	${SUDO} ldapd -n -r ${.OBJDIR} -f ${.OBJDIR}/ldapd1.conf
	${SUDO} ldapd -dvv -r ${.OBJDIR} -f ${.OBJDIR}/ldapd1.conf >ldapd.log 2>&1 &
	# wait up to 10 seconds for startup
	for i in `jot 100`; do \
	    if grep -q 'ldape: entering event loop' ldapd.log; then break; fi; \
	    sleep .1; \
	done
	grep 'ldape: entering event loop' ldapd.log

kill:
	-${SUDO} pkill ldapd

connect:
	ldapsearch -x -H ldapi://%2ftmp%2fldapi | diff - ${OUT}/empty.log
	ldapsearch -x -H ldap://localhost:6639 2>&1 | diff - ${OUT}/empty.log
	ldapsearch -x -H ldap://localhost:6636 2>&1 | diff - ${OUT}/empty.log

import:
	ldapadd -f ${.CURDIR}/dit-example.ldif -Dcn=admin,${DIT} -x -w secret -H ldap://localhost:6639 | diff - ${OUT}/adding.log
	ldapsearch -x -b ${DIT} -H ldap://localhost:6639 | diff - ${OUT}/example.log
	ldapsearch -x -D cn=admin,${DIT} -w secret -b ${DIT} -H ldap://localhost:6639 | diff - ${OUT}/example.log

purge:
	ldapdelete -Dcn=admin,${DIT} -x -w secret -H ldap://localhost:6639 -r ${DIT}
	ldapsearch -x -H ldap://localhost:6639  2>&1 | diff - ${OUT}/empty.log

perl:
	perl ${.CURDIR}/run-tests.pl

.include <bsd.regress.mk>
