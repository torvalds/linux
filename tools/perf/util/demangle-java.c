// SPDX-License-Identifier: GPL-2.0
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include "util.h"
#include "debug.h"
#include "symbol.h"

#include "demangle-java.h"

#include <linux/ctype.h>

enum {
	MODE_PREFIX = 0,
	MODE_CLASS  = 1,
	MODE_FUNC   = 2,
	MODE_TYPE   = 3,
	MODE_CTYPE  = 3, /* class arg */
};

#define BASE_ENT(c, n)	[c - 'A']=n
static const char *base_types['Z' - 'A' + 1] = {
	BASE_ENT('B', "byte" ),
	BASE_ENT('C', "char" ),
	BASE_ENT('D', "double" ),
	BASE_ENT('F', "float" ),
	BASE_ENT('I', "int" ),
	BASE_ENT('J', "long" ),
	BASE_ENT('S', "short" ),
	BASE_ENT('Z', "bool" ),
};

/*
 * demangle Java symbol between str and end positions and stores
 * up to maxlen characters into buf. The parser starts in mode.
 *
 * Use MODE_PREFIX to process entire prototype till end position
 * Use MODE_TYPE to process return type if str starts on return type char
 *
 *  Return:
 *	success: buf
 *	error  : NULL
 */
static char *
__demangle_java_sym(const char *str, const char *end, char *buf, int maxlen, int mode)
{
	int rlen = 0;
	int array = 0;
	int narg = 0;
	const char *q;

	if (!end)
		end = str + strlen(str);

	for (q = str; q != end; q++) {

		if (rlen == (maxlen - 1))
			break;

		switch (*q) {
		case 'L':
			if (mode == MODE_PREFIX || mode == MODE_CTYPE) {
				if (mode == MODE_CTYPE) {
					if (narg)
						rlen += scnprintf(buf + rlen, maxlen - rlen, ", ");
					narg++;
				}
				rlen += scnprintf(buf + rlen, maxlen - rlen, "class ");
				if (mode == MODE_PREFIX)
					mode = MODE_CLASS;
			} else
				buf[rlen++] = *q;
			break;
		case 'B':
		case 'C':
		case 'D':
		case 'F':
		case 'I':
		case 'J':
		case 'S':
		case 'Z':
			if (mode == MODE_TYPE) {
				if (narg)
					rlen += scnprintf(buf + rlen, maxlen - rlen, ", ");
				rlen += scnprintf(buf + rlen, maxlen - rlen, "%s", base_types[*q - 'A']);
				while (array--)
					rlen += scnprintf(buf + rlen, maxlen - rlen, "[]");
				array = 0;
				narg++;
			} else
				buf[rlen++] = *q;
			break;
		case 'V':
			if (mode == MODE_TYPE) {
				rlen += scnprintf(buf + rlen, maxlen - rlen, "void");
				while (array--)
					rlen += scnprintf(buf + rlen, maxlen - rlen, "[]");
				array = 0;
			} else
				buf[rlen++] = *q;
			break;
		case '[':
			if (mode != MODE_TYPE)
				goto error;
			array++;
			break;
		case '(':
			if (mode != MODE_FUNC)
				goto error;
			buf[rlen++] = *q;
			mode = MODE_TYPE;
			break;
		case ')':
			if (mode != MODE_TYPE)
				goto error;
			buf[rlen++] = *q;
			narg = 0;
			break;
		case ';':
			if (mode != MODE_CLASS && mode != MODE_CTYPE)
				goto error;
			/* safe because at least one other char to process */
			if (isalpha(*(q + 1)))
				rlen += scnprintf(buf + rlen, maxlen - rlen, ".");
			if (mode == MODE_CLASS)
				mode = MODE_FUNC;
			else if (mode == MODE_CTYPE)
				mode = MODE_TYPE;
			break;
		case '/':
			if (mode != MODE_CLASS && mode != MODE_CTYPE)
				goto error;
			rlen += scnprintf(buf + rlen, maxlen - rlen, ".");
			break;
		default :
			buf[rlen++] = *q;
		}
	}
	buf[rlen] = '\0';
	return buf;
error:
	return NULL;
}

/*
 * Demangle Java function signature (openJDK, not GCJ)
 * input:
 * 	str: string to parse. String is not modified
 *    flags: comobination of JAVA_DEMANGLE_* flags to modify demangling
 * return:
 *	if input can be demangled, then a newly allocated string is returned.
 *	if input cannot be demangled, then NULL is returned
 *
 * Note: caller is responsible for freeing demangled string
 */
char *
java_demangle_sym(const char *str, int flags)
{
	char *buf, *ptr;
	char *p;
	size_t len, l1 = 0;

	if (!str)
		return NULL;

	/* find start of retunr type */
	p = strrchr(str, ')');
	if (!p)
		return NULL;

	/*
	 * expansion factor estimated to 3x
	 */
	len = strlen(str) * 3 + 1;
	buf = malloc(len);
	if (!buf)
		return NULL;

	buf[0] = '\0';
	if (!(flags & JAVA_DEMANGLE_NORET)) {
		/*
		 * get return type first
		 */
		ptr = __demangle_java_sym(p + 1, NULL, buf, len, MODE_TYPE);
		if (!ptr)
			goto error;

		/* add space between return type and function prototype */
		l1 = strlen(buf);
		buf[l1++] = ' ';
	}

	/* process function up to return type */
	ptr = __demangle_java_sym(str, p + 1, buf + l1, len - l1, MODE_PREFIX);
	if (!ptr)
		goto error;

	return buf;
error:
	free(buf);
	return NULL;
}
