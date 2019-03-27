/*-
 * Copyright (c) 2009 David Schultz <das@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#define	CHUNK_MAX	10

/* The assertions depend on this string. */
char apothegm[] = "All work and no play\0 makes Jack a dull boy.\n";

/*
 * This is a neurotic reader function designed to give getdelim() a
 * hard time. It reads through the string `apothegm' and returns a
 * random number of bytes up to the requested length.
 */
static int
_reader(void *cookie, char *buf, int len)
{
	size_t *offp = cookie;
	size_t r;

	r = random() % CHUNK_MAX + 1;
	if (len > r)
		len = r;
	if (len > sizeof(apothegm) - *offp)
		len = sizeof(apothegm) - *offp;
	memcpy(buf, apothegm + *offp, len);
	*offp += len;
	return (len);
}

static FILE *
mkfilebuf(void)
{
	size_t *offp;

	offp = malloc(sizeof(*offp));	/* XXX leak */
	*offp = 0;
	return (fropen(offp, _reader));
}

ATF_TC_WITHOUT_HEAD(getline_basic);
ATF_TC_BODY(getline_basic, tc)
{
	FILE *fp;
	char *line;
	size_t linecap;
	int i;

	srandom(0);

	/*
	 * Test multiple times with different buffer sizes
	 * and different _reader() return values.
	 */
	errno = 0;
	for (i = 0; i < 8; i++) {
		fp = mkfilebuf();
		linecap = i;
		line = malloc(i);
		/* First line: the full apothegm */
		ATF_REQUIRE(getline(&line, &linecap, fp) == sizeof(apothegm) - 1);
		ATF_REQUIRE(memcmp(line, apothegm, sizeof(apothegm)) == 0);
		ATF_REQUIRE(linecap >= sizeof(apothegm));
		/* Second line: the NUL terminator following the newline */
		ATF_REQUIRE(getline(&line, &linecap, fp) == 1);
		ATF_REQUIRE(line[0] == '\0' && line[1] == '\0');
		/* Third line: EOF */
		line[0] = 'X';
		ATF_REQUIRE(getline(&line, &linecap, fp) == -1);
		ATF_REQUIRE(line[0] == '\0');
		free(line);
		line = NULL;
		ATF_REQUIRE(feof(fp));
		ATF_REQUIRE(!ferror(fp));
		fclose(fp);
	}
	ATF_REQUIRE(errno == 0);
}

ATF_TC_WITHOUT_HEAD(stream_error);
ATF_TC_BODY(stream_error, tc)
{
	char *line;
	size_t linecap;

	/* Make sure read errors are handled properly. */
	line = NULL;
	linecap = 0;
	errno = 0;
	ATF_REQUIRE(getline(&line, &linecap, stdout) == -1);
	ATF_REQUIRE(errno == EBADF);
	errno = 0;
	ATF_REQUIRE(getdelim(&line, &linecap, 'X', stdout) == -1);
	ATF_REQUIRE(errno == EBADF);
	ATF_REQUIRE(ferror(stdout));
}

ATF_TC_WITHOUT_HEAD(invalid_params);
ATF_TC_BODY(invalid_params, tc)
{
	FILE *fp;
	char *line;
	size_t linecap;

	/* Make sure NULL linep or linecapp pointers are handled. */
	fp = mkfilebuf();
	ATF_REQUIRE(getline(NULL, &linecap, fp) == -1);
	ATF_REQUIRE(errno == EINVAL);
	ATF_REQUIRE(getline(&line, NULL, fp) == -1);
	ATF_REQUIRE(errno == EINVAL);
	ATF_REQUIRE(ferror(fp));
	fclose(fp);
}

ATF_TC_WITHOUT_HEAD(eof);
ATF_TC_BODY(eof, tc)
{
	FILE *fp;
	char *line;
	size_t linecap;

	/* Make sure getline() allocates memory as needed if fp is at EOF. */
	errno = 0;
	fp = mkfilebuf();
	while (!feof(fp))	/* advance to EOF; can't fseek this stream */
		getc(fp);
	line = NULL;
	linecap = 0;
	printf("getline\n");
	ATF_REQUIRE(getline(&line, &linecap, fp) == -1);
	ATF_REQUIRE(line[0] == '\0');
	ATF_REQUIRE(linecap > 0);
	ATF_REQUIRE(errno == 0);
	printf("feof\n");
	ATF_REQUIRE(feof(fp));
	ATF_REQUIRE(!ferror(fp));
	fclose(fp);
}

ATF_TC_WITHOUT_HEAD(nul);
ATF_TC_BODY(nul, tc)
{
	FILE *fp;
	char *line;
	size_t linecap, n;

	errno = 0;
	line = NULL;
	linecap = 0;
	/* Make sure a NUL delimiter works. */
	fp = mkfilebuf();
	n = strlen(apothegm);
	printf("getdelim\n");
	ATF_REQUIRE(getdelim(&line, &linecap, '\0', fp) == n + 1);
	ATF_REQUIRE(strcmp(line, apothegm) == 0);
	ATF_REQUIRE(line[n + 1] == '\0');
	ATF_REQUIRE(linecap > n + 1);
	n = strlen(apothegm + n + 1);
	printf("getdelim 2\n");
	ATF_REQUIRE(getdelim(&line, &linecap, '\0', fp) == n + 1);
	ATF_REQUIRE(line[n + 1] == '\0');
	ATF_REQUIRE(linecap > n + 1);
	ATF_REQUIRE(errno == 0);
	ATF_REQUIRE(!ferror(fp));
	fclose(fp);
}

ATF_TC_WITHOUT_HEAD(empty_NULL_buffer);
ATF_TC_BODY(empty_NULL_buffer, tc)
{
	FILE *fp;
	char *line;
	size_t linecap;

	/* Make sure NULL *linep and zero *linecapp are handled. */
	fp = mkfilebuf();
	line = NULL;
	linecap = 42;
	ATF_REQUIRE(getline(&line, &linecap, fp) == sizeof(apothegm) - 1);
	ATF_REQUIRE(memcmp(line, apothegm, sizeof(apothegm)) == 0);
	fp = mkfilebuf();
	free(line);
	line = malloc(100);
	linecap = 0;
	ATF_REQUIRE(getline(&line, &linecap, fp) == sizeof(apothegm) - 1);
	ATF_REQUIRE(memcmp(line, apothegm, sizeof(apothegm)) == 0);
	free(line);
	ATF_REQUIRE(!ferror(fp));
	fclose(fp);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getline_basic);
	ATF_TP_ADD_TC(tp, stream_error);
	ATF_TP_ADD_TC(tp, eof);
	ATF_TP_ADD_TC(tp, invalid_params);
	ATF_TP_ADD_TC(tp, nul);
	ATF_TP_ADD_TC(tp, empty_NULL_buffer);

	return (atf_no_error());
}
