#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <crypt.h>
#include <unistd.h>

#include <atf-c.h>

#define	LEET "0.s0.l33t"

ATF_TC(md5);
ATF_TC_HEAD(md5, tc)
{

	atf_tc_set_md_var(tc, "descr", "Tests the MD5 based password hash");
}

ATF_TC_BODY(md5, tc)
{
	const char want[] = "$1$deadbeef$0Huu6KHrKLVWfqa4WljDE0";
	char *pw;

	pw = crypt(LEET, want);
	ATF_CHECK_STREQ(pw, want);
}

ATF_TC(invalid);
ATF_TC_HEAD(invalid, tc)
{

	atf_tc_set_md_var(tc, "descr", "Tests that invalid password fails");
}

ATF_TC_BODY(invalid, tc)
{
	const char want[] = "$1$cafebabe$0Huu6KHrKLVWfqa4WljDE0";
	char *pw;

	pw = crypt(LEET, want);
	ATF_CHECK(strcmp(pw, want) != 0);
}

/*
 * This function must not do anything except enumerate
 * the test cases, per atf-c-api(3).
 */
ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, md5);
	ATF_TP_ADD_TC(tp, invalid);
	return atf_no_error();
}
