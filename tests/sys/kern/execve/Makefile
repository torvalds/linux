# $FreeBSD$

TESTSDIR=	${TESTSBASE}/sys/kern/execve

BINDIR=		${TESTSDIR}

MAN=

ATF_TESTS_SH+=	execve_test

PROGS+=		good_aout
PROGS+=		execve_helper

LDFLAGS.goodaout+=	-static

CLEANFILES+=	empty
CLEANFILES+=	sparse_aout
CLEANFILES+=	trunc_aout

SCRIPTS+=	bad_interp_len
SCRIPTS+=	dev_null_script
SCRIPTS+=	empty
SCRIPTS+=	good_script
SCRIPTS+=	non_exist_shell
SCRIPTS+=	script_arg
SCRIPTS+=	script_arg_nospace
SCRIPTS+=	sparse_aout
SCRIPTS+=	trunc_aout

empty:
	@touch $@

sparse_aout:
	@truncate -s 20480 $@

trunc_aout:
	@truncate -s 16 $@

.include <bsd.test.mk>
