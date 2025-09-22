/* $OpenBSD: strerror_r.c,v 1.13 2016/10/19 16:26:16 bluhm Exp $ */
/* Public Domain <marc@snafu.org> */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <string.h>

static size_t
__digits10(unsigned int num)
{
	size_t i = 0;

	do {
		num /= 10;
		i++;
	} while (num != 0);

	return i;
}

static int
__itoa(int num, int sign, char *buffer, size_t start, size_t end)
{
	size_t pos;
	unsigned int a;
	int neg;

	if (sign && num < 0) {
		a = -num;
		neg = 1;
	}
	else {
		a = num;
		neg = 0;
	}

	pos = start + __digits10(a);
	if (neg)
	    pos++;

	if (pos < end)
		buffer[pos] = '\0';
	else
		return ERANGE;
	pos--;
	do {
		buffer[pos] = (a % 10) + '0';
		pos--;
		a /= 10;
	} while (a != 0);
	if (neg)
		buffer[pos] = '-';
	return 0;
}


static int
__num2string(int num, int sign, int setid, char *buf, size_t buflen,
    const char * const list[], size_t max, const char *def)
{
	int ret = 0;
	size_t len;

	if (0 <= num && num < max) {
		len = strlcpy(buf, list[num], buflen);
		if (len >= buflen)
			ret = ERANGE;
	} else {
		len = strlcpy(buf, def, buflen);
		if (len >= buflen)
			ret = ERANGE;
		else {
			ret = __itoa(num, sign, buf, len, buflen);
			if (ret == 0)
				ret = EINVAL;
		}
	}

	return ret;
}

#define	UPREFIX	"Unknown error: "

int
strerror_r(int errnum, char *strerrbuf, size_t buflen)
{
	int ret_errno;

	ret_errno = __num2string(errnum, 1, 1, strerrbuf, buflen,
	    sys_errlist, sys_nerr, UPREFIX);

	if (ret_errno)
		errno = ret_errno;
	return (ret_errno);
}
DEF_WEAK(strerror_r);

#define USIGPREFIX "Unknown signal: "

char *
__strsignal(int num, char *buf)
{
	__num2string(num, 0, 2, buf, NL_TEXTMAX, sys_siglist, NSIG,
	    USIGPREFIX);
	return buf;
}
