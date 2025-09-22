/* $OpenBSD: fmt_test.c,v 1.19 2022/12/04 23:50:46 cheloha Exp $ */

/*
 * Combined tests for fmt_scaled and scan_scaled.
 * Ian Darwin, January 2001. Public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include <util.h>

static int fmt_test(void);
static int scan_test(void);

static void print_errno(int e);
static int assert_int(int testnum, int checknum, int expect, int result);
static int assert_errno(int testnum, int checknum, int expect, int result);
static int assert_llong(int testnum, int checknum, long long expect, long long result);
static int assert_str(int testnum, int checknum, char * expect, char * result);

extern char *__progname;
static int verbose = 0;

__dead static void usage(int stat)
{
	fprintf(stderr, "usage: %s [-v]\n", __progname);
	exit(stat);
}

int
main(int argc, char **argv)
{
	int i, ch;
 
	while ((ch = getopt(argc, argv, "hv")) != -1) {
			switch (ch) {
			case 'v':
					verbose = 1;
					break;
			case 'h':
					usage(0);
			default:
					usage(1);
			}
	}
	argc -= optind;
	argv += optind;

	if (verbose)
		printf("Starting fmt_test\n");
	i = fmt_test();
	if (verbose)
		printf("Starting scan_test\n");
	i += scan_test();
	if (i) {
		printf("*** %d errors in libutil/fmt_scaled tests ***\n", i);
	} else {
		if (verbose)
			printf("Tests done; no unexpected errors\n");
	}
	return i;
}

/************** tests for fmt_scaled *******************/

static struct {			/* the test cases */
	long long input;
	char *expect;
	int err;
} ddata[] = {
	{ 0, "0B", 0 },
	{ 1, "1B", 0 },
	{ -1, "-1B", 0 },
	{ 100, "100B", 0},
	{ -100, "-100B", 0},
	{ 999, "999B", 0 },
	{ 1000, "1000B", 0 },
	{ 1023, "1023B", 0 },
	{ -1023, "-1023B", 0 },
	{ 1024, "1.0K", 0 },
	{ 1025, "1.0K", 0 },
	{ 1234, "1.2K", 0 },
	{ -1234, "-1.2K", 0 },
	{ 1484, "1.4K", 0 },		/* rounding boundary, down */
	{ 1485, "1.5K", 0 },		/* rounding boundary, up   */
	{ -1484, "-1.4K", 0 },		/* rounding boundary, down */
	{ -1485, "-1.5K", 0 },		/* rounding boundary, up   */
	{ 1536, "1.5K", 0 },
	{ 1786, "1.7K", 0 },
	{ 1800, "1.8K", 0 },
	{ 2000, "2.0K", 0 },
	{ 123456, "121K", 0 },
	{ 578318, "565K", 0 },
	{ 902948, "882K", 0 },
	{ 1048576, "1.0M", 0},
	{ 1048628, "1.0M", 0},
	{ 1049447, "1.0M", 0},
	{ -102400, "-100K", 0},
	{ -103423, "-101K", 0 },
	{ 7299072, "7.0M", 0 },
	{ 409478144L, "391M", 0 },
	{ -409478144L, "-391M", 0 },
	{ 999999999L, "954M", 0 },
	{ 1499999999L, "1.4G", 0 },
	{ 12475423744LL, "11.6G", 0},
	{ 1LL<<61, "2.0E", 0 },
	{ 1LL<<62, "4.0E", 0 },
	{ 1LL<<63, "", ERANGE },
	{ 1099512676352LL, "1.0T", 0}
};
#	define DDATA_LENGTH (sizeof ddata/sizeof *ddata)

static int
fmt_test(void)
{
	unsigned int i, e, errs = 0;
	int ret;
	char buf[FMT_SCALED_STRSIZE];

	for (i = 0; i < DDATA_LENGTH; i++) {
		strlcpy(buf, "UNSET", FMT_SCALED_STRSIZE);
		errno = 0;
		ret = fmt_scaled(ddata[i].input, buf);
		e = errno;
		if (verbose) {
			printf("%lld --> %s (%d)", ddata[i].input, buf, ret);
			if (ret == -1)
				print_errno(e);
			printf("\n");
		}
		if (ret == -1)
			errs += assert_int(i, 1, ret, ddata[i].err == 0 ? 0 : -1);
		if (ddata[i].err)
			errs += assert_errno(i, 2, ddata[i].err, e);
		else
			errs += assert_str(i, 3, ddata[i].expect, buf);
	}

	return errs;
}

/************** tests for scan_scaled *******************/


#define	IMPROBABLE	(-42)

struct {					/* the test cases */
	char *input;
	long long result;
	int err;
} sdata[] = {
	{ "0",		0, 0 },
	{ "123",	123, 0 },
	{ "1k",		1024, 0 },		/* lower case */
	{ "100.944", 100, 0 },	/* should --> 100 (truncates fraction) */
	{ "10099",	10099LL, 0 },
	{ "1M",		1048576LL, 0 },
	{ "1.1M",	1153433LL, 0 },		/* fractions */
	{ "1.111111111111111111M",	1165084LL, 0 },		/* fractions */
	{ "1.55M",	1625292LL, 0 },	/* fractions */
	{ "1.9M",	1992294LL, 0 },		/* fractions */
	{ "-2K",	-2048LL, 0 },		/* negatives */
	{ "-2.2K",	-2252LL, 0 },	/* neg with fract */
	{ "4.5k", 4608, 0 },
	{ "3.333755555555t", 3665502936412, 0 },
	{ "-3.333755555555t", -3665502936412, 0 },
	{ "4.5555555555555555K", 4664, 0 },
	{ "4.5555555555555555555K", 4664, 0 },	/* handle enough digits? */
	{ "4.555555555555555555555555555555K", 4664, 0 }, /* ignores extra digits? */
	{ "1G",		1073741824LL, 0 },
	{ "G", 		0, 0 },			/* should == 0G? */
	{ "1234567890", 1234567890LL, 0 },	/* should work */
	{ "1.5E",	1729382256910270464LL, 0 },		/* big */
	{ "32948093840918378473209480483092", 0, ERANGE },  /* too big */
	{ "1.5Q",	0, EINVAL },		/* invalid multiplier */
	{ "1ab",	0, EINVAL },		/* ditto */
	{ "3&",		0, EINVAL },		/* ditto */
	{ "5.0e3",	0, EINVAL },	/* digits after */
	{ "5.0E3",	0, EINVAL },	/* ditto */
	{ "1..0",	0, EINVAL },		/* bad format */
	{ "",		0, 0 },			/* boundary */
	{ "--1", -1, EINVAL },
	{ "++42", -1, EINVAL },
	{ "-.060000000000000000E", -69175290276410818, 0 },
	{ "-.600000000000000000E", -691752902764108185, 0 },
	{ "-60000000000000000E", 0, ERANGE },
	{ "SCALE_OVERFLOW", 0, ERANGE },
	{ "SCALE_UNDERFLOW", 0, ERANGE },
	{ "LLONG_MAX_K", (LLONG_MAX / 1024) * 1024, 0 },
	{ "LLONG_MIN_K", (LLONG_MIN / 1024) * 1024, 0 },
	{ "LLONG_MAX", LLONG_MAX, 0 },	/* upper limit */

	/*
	 * Lower limit is a bit special: because scan_scaled accumulates into a
	 * signed long long it can only handle up to the negative value of
	 * LLONG_MAX not LLONG_MIN.
	 */
	{ "NEGATIVE_LLONG_MAX", LLONG_MAX*-1, 0 },	/* lower limit */
	{ "LLONG_MIN", 0, ERANGE },	/* can't handle */
#if LLONG_MAX == 0x7fffffffffffffffLL
	{ "-9223372036854775807", -9223372036854775807, 0 },
	{ "9223372036854775807", 9223372036854775807, 0 },
	{ "9223372036854775808", 0, ERANGE },
	{ "9223372036854775809", 0, ERANGE },
#endif
#if LLONG_MIN == (-0x7fffffffffffffffLL-1)
	{ "-9223372036854775808", 0, ERANGE },
	{ "-9223372036854775809", 0, ERANGE },
	{ "-9223372036854775810", 0, ERANGE },
#endif
};
#	define SDATA_LENGTH (sizeof sdata/sizeof *sdata)

static void
print_errno(int e)
{
	switch(e) {
		case EINVAL: printf("EINVAL"); break;
		case EDOM:   printf("EDOM"); break;
		case ERANGE: printf("ERANGE"); break;
		default: printf("errno %d", e);
	}
}

/** Print one result */
static void
print(char *input, long long result, int ret, int e)
{
	printf("\"%40s\" --> %lld (%d)", input, result, ret);
	if (ret == -1) {
		printf(" -- ");
		print_errno(e);
	}
	printf("\n");
}

static int
scan_test(void)
{
	unsigned int i, errs = 0, e;
	int ret;
	long long result;
	char buf[1024], *input;

	for (i = 0; i < SDATA_LENGTH; i++) {
		result = IMPROBABLE;

		input = sdata[i].input;
		/* some magic values for architecture dependent limits */
		if (strcmp(input, "LLONG_MAX") == 0) {
			snprintf(buf, sizeof buf," %lld", LLONG_MAX);
			input = buf;
		} else if (strcmp(input, "LLONG_MIN") == 0) {
			snprintf(buf, sizeof buf," %lld", LLONG_MIN);
			input = buf;
		} else if (strcmp(input, "LLONG_MAX_K") == 0) {
			snprintf(buf, sizeof buf," %lldK", LLONG_MAX/1024);
			input = buf;
		} else if (strcmp(input, "LLONG_MIN_K") == 0) {
			snprintf(buf, sizeof buf," %lldK", LLONG_MIN/1024);
			input = buf;
		} else if (strcmp(input, "SCALE_OVERFLOW") == 0) {
			snprintf(buf, sizeof buf," %lldK", (LLONG_MAX/1024)+1);
			input = buf;
		} else if (strcmp(input, "SCALE_UNDERFLOW") == 0) {
			snprintf(buf, sizeof buf," %lldK", (LLONG_MIN/1024)-1);
			input = buf;
		} else if (strcmp(input, "NEGATIVE_LLONG_MAX") == 0) {
			snprintf(buf, sizeof buf," %lld", LLONG_MAX*-1);
			input = buf;
		}
		if (verbose && input != sdata[i].input)
			printf("expand '%s' -> '%s'\n", sdata[i].input,
			    input);

		/* printf("Calling scan_scaled(%s, ...)\n", sdata[i].input); */
		errno = 0;
		ret = scan_scaled(input, &result);
		e = errno;	/* protect across printfs &c. */
		if (verbose)
			print(input, result, ret, e);
		if (ret == -1)
			errs += assert_int(i, 1, ret, sdata[i].err == 0 ? 0 : -1);
		if (sdata[i].err)
			errs += assert_errno(i, 2, sdata[i].err, e);
		else 
			errs += assert_llong(i, 3, sdata[i].result, result);
	}
	return errs;
}

/************** common testing stuff *******************/

static int
assert_int(int testnum, int check, int expect, int result)
{
	if (expect == result)
		return 0;
	printf("** FAILURE: test %d check %d, expect %d, result %d **\n",
		testnum, check, expect, result);
	return 1;
}

static int
assert_errno(int testnum, int check, int expect, int result)
{
	if (expect == result)
		return 0;
	printf("** FAILURE: test %d check %d, expect ",
		testnum, check);
	print_errno(expect);
	printf(", got ");
	print_errno(result);
	printf(" **\n");
	return 1;
}

static int
assert_llong(int testnum, int check, long long expect, long long result)
{
	if (expect == result)
		return 0;
	printf("** FAILURE: test %d check %d, expect %lld, result %lld **\n",
		testnum, check, expect, result);
	return 1;
}

static int
assert_str(int testnum, int check, char * expect, char * result)
{
	if (strcmp(expect, result) == 0)
		return 0;
	printf("** FAILURE: test %d check %d, expect %s, result %s **\n",
		testnum, check, expect, result);
	return 1;
}
