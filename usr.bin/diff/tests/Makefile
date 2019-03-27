# $FreeBSD$

PACKAGE=	tests

ATF_TESTS_SH=	diff_test

${PACKAGE}FILES+=	\
	Bflag_C.out \
	Bflag_D.out \
	Bflag_F.out \
	input1.in \
	input2.in \
	input_c1.in \
	input_c2.in \
	simple.out \
	simple_e.out \
	simple_n.out \
	simple_u.out \
	simple_i.out \
	simple_w.out \
	simple_b.out \
	simple_p.out \
	unified_p.out \
	unified_c9999.out \
	unified_9999.out \
	header.out \
	header_ns.out \
	ifdef.out \
	group-format.out

NETBSD_ATF_TESTS_SH+=	netbsd_diff_test

${PACKAGE}FILES+=	\
	d_mallocv1.in	\
	d_mallocv2.in

ATF_TESTS_SH_SED_netbsd_diff_test+=	-e 's/t_diff/`basename $$0`/g'
ATF_TESTS_SH_SRC_netbsd_diff_test=	t_diff.sh

.include <netbsd-tests.test.mk>

.include <bsd.test.mk>
