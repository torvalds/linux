#include <string.h>
#include <stdlib.h>
#include "string.h"
#include "util.h"

static int hex(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	return -1;
}

/*
 * While we find nice hex chars, build a long_val.
 * Return number of chars processed.
 */
int hex2u64(const char *ptr, u64 *long_val)
{
	const char *p = ptr;
	*long_val = 0;

	while (*p) {
		const int hex_val = hex(*p);

		if (hex_val < 0)
			break;

		*long_val = (*long_val << 4) | hex_val;
		p++;
	}

	return p - ptr;
}

char *strxfrchar(char *s, char from, char to)
{
	char *p = s;

	while ((p = strchr(p, from)) != NULL)
		*p++ = to;

	return s;
}

#define K 1024LL
/*
 * perf_atoll()
 * Parse (\d+)(b|B|kb|KB|mb|MB|gb|GB|tb|TB) (e.g. "256MB")
 * and return its numeric value
 */
s64 perf_atoll(const char *str)
{
	unsigned int i;
	s64 length = -1, unit = 1;

	if (!isdigit(str[0]))
		goto out_err;

	for (i = 1; i < strlen(str); i++) {
		switch (str[i]) {
		case 'B':
		case 'b':
			break;
		case 'K':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto kilo;
		case 'k':
			if (str[i + 1] != 'b')
				goto out_err;
kilo:
			unit = K;
			break;
		case 'M':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto mega;
		case 'm':
			if (str[i + 1] != 'b')
				goto out_err;
mega:
			unit = K * K;
			break;
		case 'G':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto giga;
		case 'g':
			if (str[i + 1] != 'b')
				goto out_err;
giga:
			unit = K * K * K;
			break;
		case 'T':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto tera;
		case 't':
			if (str[i + 1] != 'b')
				goto out_err;
tera:
			unit = K * K * K * K;
			break;
		case '\0':	/* only specified figures */
			unit = 1;
			break;
		default:
			if (!isdigit(str[i]))
				goto out_err;
			break;
		}
	}

	length = atoll(str) * unit;
	goto out;

out_err:
	length = -1;
out:
	return length;
}
