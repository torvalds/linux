#	$OpenBSD: Makefile,v 1.6 2024/01/16 06:40:07 anton Exp $

REGRESS_TARGETS=	hit miss cleanup flags
CLEANFILES=		stamp-*	\
			pf-instab.conf		\
			pf-instance.conf	\
			pf-reftab.conf		\
			table-ref.conf		\
			table-pgone.out		\
			table-persist.out	\
			table-ref.out		\
			table-refgone.out


stamp-setup:
	${SUDO} pfctl -q -a regress/ttest -Fa || exit 0
	${SUDO} pfctl -qt __regress_tbl -T add -f ${.CURDIR}/table.in
	date >$@

cleanup:
	rm -f stamp-setup
	${SUDO} pfctl -qt __regress_tbl -T kill
	${SUDO} pfctl -q -a regress/ttest -Fr
	${SUDO} pfctl -q -a regress/ttest -qt instance -T kill

hit: stamp-setup
	for i in `cat ${.CURDIR}/table.hit`; do \
		echo -n "hit $$i "; \
		${SUDO} pfctl -qt __regress_tbl -T test $$i || exit 1; \
		echo success; \
	done; \
	exit 0

miss: stamp-setup
	for i in `cat ${.CURDIR}/table.miss`; do \
		echo -n "miss $$i "; \
		${SUDO} pfctl -qt __regress_tbl -T test $$i && exit 1; \
		echo success; \
	done; \
	exit 0

#
# tables <instance> and <reference> are both referenced by rule only
#
pf-instab.conf:
	@echo 'table <instance> { 192.168.1.0/24 }' > $@
	@echo 'pass in from <instance> to <reference>' >> $@

#
# table <instance> is active and referred by rule, table <reference>
# is referenced only.
pf-reftab.conf:
	@echo 'pass in from <instance> to <reference>' > $@

#
# check persistent flag (p) is gone from table <instance> after
# we load pf-instab.conf. Deals with case when persistent table <instance>
# exists before pf-instab.conf gets loaded.
#
table-pgone.out:
	@echo '--a-r--	instance@regress/ttest' > $@
	@echo '----r--	reference@regress/ttest' >> $@

#
# verify table <instance> got persistent flag after we
# run 'pfctl -t instance -T add ...'
#
table-persist.out:
	@echo '-pa-r--	instance@regress/ttest' > $@
	@echo '----r--	reference@regress/ttest' >> $@

#
# verify tables <instance> and <reference> are created on behalf of
# reference by rule after pf-reftab.conf got loaded.
#
table-ref.out:
	@echo '----r--	instance@regress/ttest' > $@
	@echo '----r--	reference@regress/ttest' >> $@

#
# verify reference to <instance> table (persistent) is gone
# after rules got flushed
#
table-refgone.out:
	@echo '-pa----	instance@regress/ttest' > $@

flags: pf-instab.conf pf-reftab.conf table-pgone.out table-persist.out \
    table-ref.out table-refgone.out
	@echo 'loading pf-reftab,conf (tables referenced by rules only)'
	@cat pf-reftab.conf
	${SUDO} pfctl -a regress/ttest -f pf-reftab.conf
	@echo 'tables <reference> and <instance> should both have ----r--'
	${SUDO} pfctl -a regress/ttest -sT -vg | diff table-ref.out -
	@echo 'creating <instance> table on command line, flags should be:'
	@cat table-persist.out
	${SUDO} pfctl -a regress/ttest -t instance -T add 192.168.1.0/24
	${SUDO} pfctl -a regress/ttest -sT -vg | diff table-persist.out -
	@echo 'flushing rules'
	${SUDO} pfctl -a regress/ttest -Fr
	@echo 'table <reference> should be gone, table <instance> should stay'
	${SUDO} pfctl -a regress/ttest -sT -vg | diff table-refgone.out -
	@echo 'loading pf-instab.conf'
	@cat pf-instab.conf
	${SUDO} pfctl -a regress/ttest -f pf-instab.conf
	@echo 'table <instance> loses -p- flag:'
	@cat table-pgone.out
	${SUDO} pfctl -a regress/ttest -sT -vg | diff table-pgone.out -
	@echo 'flusing rules, both tables should be gone'
	${SUDO} pfctl -a regress/ttest -Fr
	@echo 'anchor regress/ttest must be gone'
	${SUDO} pfctl -a regress/ttest -sr 2>&1 | grep 'pfctl: Anchor does not exist'

.PHONY: hit miss flags

.include <bsd.regress.mk>
