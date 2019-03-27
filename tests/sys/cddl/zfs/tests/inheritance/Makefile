# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/inheritance
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	inheritance_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	config015.cfg
${PACKAGE}FILES+=	config012.cfg
${PACKAGE}FILES+=	state007.cfg
${PACKAGE}FILES+=	state009.cfg
${PACKAGE}FILES+=	config020.cfg
${PACKAGE}FILES+=	state024.cfg
${PACKAGE}FILES+=	state023.cfg
${PACKAGE}FILES+=	state018.cfg
${PACKAGE}FILES+=	state016.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	config003.cfg
${PACKAGE}FILES+=	state011.cfg
${PACKAGE}FILES+=	config004.cfg
${PACKAGE}FILES+=	config021.cfg
${PACKAGE}FILES+=	state008.cfg
${PACKAGE}FILES+=	config013.cfg
${PACKAGE}FILES+=	state006.cfg
${PACKAGE}FILES+=	config014.cfg
${PACKAGE}FILES+=	state001.cfg
${PACKAGE}FILES+=	state010.cfg
${PACKAGE}FILES+=	config005.cfg
${PACKAGE}FILES+=	state017.cfg
${PACKAGE}FILES+=	config002.cfg
${PACKAGE}FILES+=	state019.cfg
${PACKAGE}FILES+=	state022.cfg
${PACKAGE}FILES+=	state014.cfg
${PACKAGE}FILES+=	config001.cfg
${PACKAGE}FILES+=	state013.cfg
${PACKAGE}FILES+=	config006.cfg
${PACKAGE}FILES+=	config008.cfg
${PACKAGE}FILES+=	inherit_001_pos.ksh
${PACKAGE}FILES+=	state021.cfg
${PACKAGE}FILES+=	config022.cfg
${PACKAGE}FILES+=	config019.cfg
${PACKAGE}FILES+=	config017.cfg
${PACKAGE}FILES+=	state002.cfg
${PACKAGE}FILES+=	config010.cfg
${PACKAGE}FILES+=	state005.cfg
${PACKAGE}FILES+=	state020.cfg
${PACKAGE}FILES+=	inherit.kshlib
${PACKAGE}FILES+=	config009.cfg
${PACKAGE}FILES+=	state012.cfg
${PACKAGE}FILES+=	config007.cfg
${PACKAGE}FILES+=	state015.cfg
${PACKAGE}FILES+=	config011.cfg
${PACKAGE}FILES+=	inherit.cfg
${PACKAGE}FILES+=	state004.cfg
${PACKAGE}FILES+=	config016.cfg
${PACKAGE}FILES+=	state003.cfg
${PACKAGE}FILES+=	config018.cfg
${PACKAGE}FILES+=	config023.cfg
${PACKAGE}FILES+=	config024.cfg

.include <bsd.test.mk>
