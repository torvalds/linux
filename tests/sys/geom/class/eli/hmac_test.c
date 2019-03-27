/*
 * $FreeBSD$
 */

#include <sys/param.h>
#include <atf-c.h>

#include <geom/eli/pkcs5v2.h>

const struct {
	char	*salt;
	size_t	saltlen;
	char	*passwd;
	int	iterations;
	char	*hmacout;
	size_t	hmaclen;
} testdata[] = {
#include "testvect.h"
};

ATF_TC_WITHOUT_HEAD(hmactest);
ATF_TC_BODY(hmactest, tc)
{
	size_t i;
	uint8_t hmacout[64];

	for (i = 0; i < nitems(testdata); i++) {
		pkcs5v2_genkey(hmacout, testdata[i].hmaclen,
		    (uint8_t *)testdata[i].salt, testdata[i].saltlen,
		    testdata[i].passwd, testdata[i].iterations);
		ATF_REQUIRE(bcmp(hmacout, testdata[i].hmacout,
		    testdata[i].hmaclen) == 0);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, hmactest);

	return (atf_no_error());
}
