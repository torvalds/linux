#	$OpenBSD: Makefile,v 1.10 2025/05/10 06:10:01 anton Exp $

BIOCTL ?=		/sbin/bioctl

CHUNKFILE =		disk.img
CHUNKDEVFILE =		chunk.txt
VOLDEVFILE =		vol.txt
ROUNDSFILE =		rounds.txt
PASSFILE =		passphrase.txt

OLDPW =			oldsecret
NEWPW =			securenew
NROUNDS ?=		17

REGRESS_SETUP_ONCE =	create-chunk create-passfile

create-chunk:
	dd if=/dev/zero of=${CHUNKFILE} bs=512k count=0 seek=1 status=none
	${SUDO} vnconfig -- ${CHUNKFILE} 1>${CHUNKDEVFILE}
	echo 'RAID *' | ${SUDO} disklabel -wAT- -- "$$(<${CHUNKDEVFILE})"

create-passfile:
	${SUDO} install -o root -m u=rw,go= /dev/null ${PASSFILE}
	printf '%s\n' '${OLDPW}' | ${SUDO} tee ${PASSFILE} 1>/dev/null


SUCCESS_TESTS =			scripted-create-volume \
				scripted-recreate-volume \
				reopen-volume-with-passfile \
				scripted-change-passphrase \
				verify-increased-rounds
REGRESS_TARGETS =		${SUCCESS_TESTS}
REGRESS_ROOT_TARGETS =		${SUCCESS_TESTS}

scripted-create-volume:
	printf '%s\n' '${OLDPW}' | \
		${SUDO} ${BIOCTL} -s -cC -l"$$(<${CHUNKDEVFILE})a" -- softraid0 | \
		grep -o 'sd[0-9]*$$' 1>${VOLDEVFILE}

scripted-recreate-volume: detach-volume
	printf '%s\n' '${OLDPW}' | \
		${SUDO} ${BIOCTL} -s -Cforce -r${NROUNDS} -cC -l"$$(<${CHUNKDEVFILE})a" -- softraid0 | \
		grep -o 'sd[0-9]*$$' 1>${VOLDEVFILE}

reopen-volume-with-passfile: detach-volume
	${SUDO} ${BIOCTL} -p${PASSFILE} -cC -l"$$(<${CHUNKDEVFILE})a" -- softraid0 | \
		grep -o 'sd[0-9]*$$' 1>${VOLDEVFILE}

scripted-change-passphrase:
	printf '%s\n%s\n' '${OLDPW}' '${NEWPW}' | \
		${SUDO} ${BIOCTL} -s -P -v -- "$$(<${VOLDEVFILE})" | \
		tee -- ${ROUNDSFILE}

verify-increased-rounds:
	awk -safe -vOLD=${NROUNDS} -- '\
		NR == 1 { old = $$7 }; \
		NR == 2 { new = $$7 }; \
		END     { exit !(OLD == old && old <= new) }' ${ROUNDSFILE}


FAILURE_TESTS =			reuse-active-chunk \
				use-public-passfile \
				use-foreign-passfile \
				set-empty-passphrase
REGRESS_EXPECTED_FAILURES +=	${FAILURE_TESTS}
REGRESS_TARGETS +=		${FAILURE_TESTS}
REGRESS_ROOT_TARGETS +=		${FAILURE_TESTS}

reuse-active-chunk:
	${SUDO} ${BIOCTL} -cC -l"$$(<${CHUNKDEVFILE})a" -- softraid0

use-public-passfile:
	${SUDO} chmod a=rwx ${PASSFILE}
	${SUDO} ${BIOCTL} -p${PASSFILE} -P -- "$$(<${VOLDEVFILE})"

use-foreign-passfile:
	${SUDO} chown build ${PASSFILE}
	${SUDO} ${BIOCTL} -p${PASSFILE} -P -- "$$(<${VOLDEVFILE})"

set-empty-passphrase:
	printf '\n' | ${SUDO} ${BIOCTL} -s -P -- "$$(<${VOLDEVFILE})"


CLEANFILES =		*.txt *.img
REGRESS_CLEANUP =	cleanup

detach-volume:
	${SUDO} ${BIOCTL} -d -- "$$(<${VOLDEVFILE})" 2>/dev/null
cleanup: detach-volume
	${SUDO} vnconfig  -u -- "$$(<${CHUNKDEVFILE})" 2>/dev/null

.include <bsd.regress.mk>
