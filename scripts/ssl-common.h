/* SPDX-License-Identifier: LGPL-2.1+ */
/*
 * SSL helper functions shared by sign-file and extract-cert.
 */

static void drain_openssl_errors(int l, int silent)
{
	const char *file;
	char buf[120];
	int e, line;

	if (ERR_peek_error() == 0)
		return;
	if (!silent)
		fprintf(stderr, "At main.c:%d:\n", l);

	while ((e = ERR_peek_error_line(&file, &line))) {
		ERR_error_string(e, buf);
		if (!silent)
			fprintf(stderr, "- SSL %s: %s:%d\n", buf, file, line);
		ERR_get_error();
	}
}

#define ERR(cond, fmt, ...)				\
	do {						\
		bool __cond = (cond);			\
		drain_openssl_errors(__LINE__, 0);	\
		if (__cond) {				\
			errx(1, fmt, ## __VA_ARGS__);	\
		}					\
	} while (0)
