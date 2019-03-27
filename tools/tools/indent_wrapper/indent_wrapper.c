/*-
 * Copyright (c) 2015 Hans Petter Selasky. All rights reserved.
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
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/queue.h>
#include <sysexits.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

extern char **environ;

static int opt_verbose;
static char *opt_diff_tool;

#define	BLOCK_SIZE	4096
#define	BLOCK_MASK	0x100
#define	BLOCK_ADD	0x200

struct block {
	TAILQ_ENTRY(block) entry;
	uint32_t length;
	uint32_t flags;
	uint8_t *data;
	uint8_t *mask;
};

typedef TAILQ_HEAD(, block) block_head_t;

static void
sigpipe(int sig)
{
}

static struct block *
alloc_block(void)
{
	struct block *pb;
	size_t size = sizeof(*pb) + (2 * BLOCK_SIZE);

	pb = malloc(size);
	if (pb == NULL)
		errx(EX_SOFTWARE, "Out of memory");
	memset(pb, 0, size);
	pb->data = (void *)(pb + 1);
	pb->mask = pb->data + BLOCK_SIZE;
	pb->length = BLOCK_SIZE;
	return (pb);
}

static int
write_block(int fd, block_head_t *ph)
{
	struct block *ptr;

	if (fd < 0)
		return (-1);

	TAILQ_FOREACH(ptr, ph, entry) {
		if (write(fd, ptr->data, ptr->length) != ptr->length)
			return (-1);
	}
	return (0);
}

static uint16_t
peek_block(block_head_t *pbh, uint64_t off)
{
	struct block *ptr;

	TAILQ_FOREACH(ptr, pbh, entry) {
		if (off < ptr->length)
			break;
		off -= ptr->length;
	}
	if (ptr == NULL)
		return (0);
	return (ptr->data[off] | (ptr->mask[off] << 8));
}

static void
set_block(block_head_t *pbh, uint64_t off, uint16_t ch)
{
	struct block *ptr;

	TAILQ_FOREACH(ptr, pbh, entry) {
		if (off < ptr->length)
			break;
		off -= ptr->length;
	}
	if (ptr == NULL)
		return;
	ptr->data[off] = ch & 0xFF;
	ptr->mask[off] = (ch >> 8) & 0xFF;
}

static uint64_t
size_block(block_head_t *pbh)
{
	struct block *ptr;
	uint64_t off = 0;

	TAILQ_FOREACH(ptr, pbh, entry)
	    off += ptr->length;
	return (off);
}

static int
diff_tool(block_head_t *pa, block_head_t *pb)
{
	char ca[] = {"/tmp/diff.orig.XXXXXX"};
	char cb[] = {"/tmp/diff.styled.XXXXXX"};
	char cc[256];
	uint64_t sa;
	uint64_t sb;
	uint64_t s;
	uint64_t x;
	int fa;
	int fb;

	sa = size_block(pa);
	sb = size_block(pb);
	s = (sa > sb) ? sa : sb;

	for (x = 0; x != s; x++) {
		char cha = peek_block(pa, x) & 0xFF;
		char chb = peek_block(pb, x) & 0xFF;

		if (cha != chb) {
			/* false positive */
			if (cha == '\n' && chb == 0 && x == sa - 1)
				return (0);
			break;
		}
	}
	if (x == s)
		return (0);		/* identical */

	fa = mkstemp(ca);
	fb = mkstemp(cb);

	if (write_block(fa, pa) < 0 || write_block(fb, pb) < 0) {
		close(fa);
		close(fb);
		unlink(ca);
		unlink(cb);
		err(EX_SOFTWARE, "Could not write data to temporary files");
	}
	close(fa);
	close(fb);

	snprintf(cc, sizeof(cc), "%s %s %s", opt_diff_tool, ca, cb);
	system(cc);

	unlink(ca);
	unlink(cb);
	return (-1);
}

static int
diff_block(block_head_t *pa, block_head_t *pb)
{
	uint64_t sa = size_block(pa);
	uint64_t sb = size_block(pb);
	uint64_t s;
	uint64_t x;
	uint64_t y;
	uint64_t n;

	s = (sa > sb) ? sa : sb;

	for (y = x = 0; x != s; x++) {
		char cha = peek_block(pa, x) & 0xFF;
		char chb = peek_block(pb, x) & 0xFF;

		if (cha != chb) {
			int nonspace;

			/* false positive */
			if (cha == '\n' && chb == 0 && x == sa - 1)
				return (0);

			n = x - y;
			printf("Style error:\n");
			nonspace = 0;
			for (n = y; n < sa; n++) {
				char ch = peek_block(pa, n) & 0xFF;

				if (nonspace && ch == '\n')
					break;
				printf("%c", ch);
				if (!isspace(ch))
					nonspace = 1;
			}
			printf("\n");
			printf("Style corrected:\n");
			nonspace = 0;
			for (n = y; n < sb; n++) {
				char ch = peek_block(pb, n) & 0xFF;

				if (nonspace && ch == '\n')
					break;
				printf("%c", ch);
				if (!isspace(ch))
					nonspace = 1;
			}
			printf("\n");
			for (n = y; n != x; n++) {
				if ((peek_block(pa, n) & 0xFF) == '\t')
					printf("\t");
				else
					printf(" ");
			}
			printf("^ %sdifference%s\n",
			    (isspace(cha) || isspace(chb)) ? "whitespace " : "",
			    (x >= sa || x >= sb) ? " in the end of a block" : "");
			return (1);
		} else if (cha == '\n') {
			y = x + 1;
		}
	}
	return (0);
}

static void
free_block(block_head_t *pbh)
{
	struct block *ptr;

	while ((ptr = TAILQ_FIRST(pbh))) {
		TAILQ_REMOVE(pbh, ptr, entry);
		free(ptr);
	}
}

static void
cmd_popen(char *command, FILE **iop)
{
	char *argv[4];
	int pdes[4];
	int pid;

	if (pipe(pdes) < 0)
		goto error;

	if (pipe(pdes + 2) < 0) {
		close(pdes[0]);
		close(pdes[1]);
		goto error;
	}
	argv[0] = "sh";
	argv[1] = "-c";
	argv[2] = command;
	argv[3] = NULL;

	switch ((pid = vfork())) {
	case -1:			/* Error. */
		close(pdes[0]);
		close(pdes[1]);
		close(pdes[2]);
		close(pdes[3]);
		goto error;
	case 0:			/* Child. */
		dup2(pdes[1], STDOUT_FILENO);
		dup2(pdes[2], STDIN_FILENO);
		close(pdes[0]);
		close(pdes[3]);
		execve("/bin/sh", argv, environ);
		exit(127);
	default:
		break;
	}
	iop[0] = fdopen(pdes[3], "w");
	iop[1] = fdopen(pdes[0], "r");
	close(pdes[1]);
	close(pdes[2]);
	return;
error:
	iop[0] = iop[1] = NULL;
}

static void
cmd_block_process(block_head_t *pbh_in, block_head_t *pbh_out, char *cmd_str)
{
	FILE *pfd[2];
	struct block *ptr;

	TAILQ_INIT(pbh_out);

	cmd_popen(cmd_str, pfd);

	if (pfd[0] == NULL || pfd[1] == NULL)
		errx(EX_SOFTWARE, "Cannot invoke command '%s'", cmd_str);

	if (pbh_in != NULL) {
		TAILQ_FOREACH(ptr, pbh_in, entry) {
			if (fwrite(ptr->data, 1, ptr->length, pfd[0]) != ptr->length)
				err(EX_SOFTWARE, "Cannot write all data to command '%s'", cmd_str);
		}
		fflush(pfd[0]);
	}
	fclose(pfd[0]);

	while (1) {
		int len;

		ptr = alloc_block();
		len = fread(ptr->data, 1, BLOCK_SIZE, pfd[1]);
		if (len <= 0) {
			free(ptr);
			break;
		}
		ptr->length = len;
		TAILQ_INSERT_TAIL(pbh_out, ptr, entry);
	}
	fclose(pfd[1]);
}

static void
usage(void)
{
	fprintf(stderr,
	    "indent_wrapper [-v] [-d] [-D] [-g <githash>]\n"
	    "\t" "[-s <svnrevision> ] [ -t <tool> ] [ -c <command> ]\n"
	    "\t" "-v        Increase verbosity\n"
	    "\t" "-d        Check output from git diff\n"
	    "\t" "-D        Check output from svn diff\n"
	    "\t" "-c <cmd>  Set custom command to produce diff\n"
	    "\t" "-g <hash> Check output from git hash\n"
	    "\t" "-s <rev>  Check output from svn revision\n"
	    "\t" "-t <tool> Launch external diff tool\n"
	    "\n"
	    "Examples:\n"
	    "\t" "indent_wrapper -D\n"
	    "\t" "indent_wrapper -D -t meld\n"
	    "\t" "indent_wrapper -D -t \"diff -u\"\n");
	exit(EX_SOFTWARE);
}

int
main(int argc, char **argv)
{
	block_head_t diff_head;
	block_head_t diff_a_head;
	block_head_t diff_b_head;
	block_head_t indent_in_head;
	block_head_t indent_out_head;
	struct block *p1 = NULL;
	struct block *p2 = NULL;
	uint64_t size;
	uint64_t x;
	uint64_t y1 = 0;
	uint64_t y2 = 0;
	int recurse = 0;
	int inside_string = 0;
	int escape_char = 0;
	int do_parse = 0;
	char cmdbuf[256];
	uint16_t ch;
	uint16_t chn;
	int c;
	int retval = 0;

	signal(SIGPIPE, &sigpipe);

	cmdbuf[0] = 0;

	while ((c = getopt(argc, argv, "dDvg:s:c:ht:")) != -1) {
		switch (c) {
		case 'v':
			opt_verbose++;
			break;
		case 't':
			opt_diff_tool = optarg;
			break;
		case 'g':
			snprintf(cmdbuf, sizeof(cmdbuf), "git show -U1000000 %s", optarg);
			break;
		case 'd':
			snprintf(cmdbuf, sizeof(cmdbuf), "git diff -U1000000");
			break;
		case 'D':
			snprintf(cmdbuf, sizeof(cmdbuf), "svn diff --diff-cmd=diff -x -U1000000");
			break;
		case 's':
			snprintf(cmdbuf, sizeof(cmdbuf), "svn diff --diff-cmd=diff -x -U1000000 -r %s", optarg);
			break;
		case 'c':
			snprintf(cmdbuf, sizeof(cmdbuf), "%s", optarg);
			break;
		default:
			usage();
		}
	}
	if (cmdbuf[0] == 0)
		usage();

	cmd_block_process(NULL, &diff_head, cmdbuf);

	TAILQ_INIT(&diff_a_head);
	TAILQ_INIT(&diff_b_head);

	size = size_block(&diff_head);
	p1 = alloc_block();
	y1 = 0;
	p2 = alloc_block();
	y2 = 0;

	for (x = 0; x < size;) {
		ch = peek_block(&diff_head, x);
		switch (ch & 0xFF) {
		case '+':
			if (ch == peek_block(&diff_head, x + 1) &&
			    ch == peek_block(&diff_head, x + 2) &&
			    ' ' == (peek_block(&diff_head, x + 3) & 0xFF))
				goto parse_filename;
			if (do_parse == 0)
				break;
			for (x++; x != size; x++) {
				ch = peek_block(&diff_head, x);
				p1->mask[y1] = BLOCK_ADD >> 8;
				p1->data[y1++] = ch;
				if (y1 == BLOCK_SIZE) {
					TAILQ_INSERT_TAIL(&diff_a_head, p1, entry);
					p1 = alloc_block();
					y1 = 0;
				}
				if ((ch & 0xFF) == '\n')
					break;
			}
			break;
		case '-':
			if (ch == peek_block(&diff_head, x + 1) &&
			    ch == peek_block(&diff_head, x + 2) &&
			    ' ' == (peek_block(&diff_head, x + 3) & 0xFF))
				goto parse_filename;
			if (do_parse == 0)
				break;
			for (x++; x != size; x++) {
				ch = peek_block(&diff_head, x);
				p2->data[y2++] = ch;
				if (y2 == BLOCK_SIZE) {
					TAILQ_INSERT_TAIL(&diff_b_head, p2, entry);
					p2 = alloc_block();
					y2 = 0;
				}
				if ((ch & 0xFF) == '\n')
					break;
			}
			break;
		case ' ':
			if (do_parse == 0)
				break;
			for (x++; x != size; x++) {
				ch = peek_block(&diff_head, x);
				p1->data[y1++] = ch;
				if (y1 == BLOCK_SIZE) {
					TAILQ_INSERT_TAIL(&diff_a_head, p1, entry);
					p1 = alloc_block();
					y1 = 0;
				}
				p2->data[y2++] = ch;
				if (y2 == BLOCK_SIZE) {
					TAILQ_INSERT_TAIL(&diff_b_head, p2, entry);
					p2 = alloc_block();
					y2 = 0;
				}
				if ((ch & 0xFF) == '\n')
					break;
			}
			break;
	parse_filename:
			for (x += 3; x != size; x++) {
				ch = peek_block(&diff_head, x);
				chn = peek_block(&diff_head, x + 1);
				if ((ch & 0xFF) == '.') {
					/* only accept .c and .h files */
					do_parse = ((chn & 0xFF) == 'c' || (chn & 0xFF) == 'h');
				}
				if ((ch & 0xFF) == '\n')
					break;
			}
		default:
			break;
		}
		/* skip till end of line */
		for (; x < size; x++) {
			ch = peek_block(&diff_head, x);
			if ((ch & 0xFF) == '\n') {
				x++;
				break;
			}
		}
	}
	p1->length = y1;
	p2->length = y2;
	TAILQ_INSERT_TAIL(&diff_a_head, p1, entry);
	TAILQ_INSERT_TAIL(&diff_b_head, p2, entry);

	/* first pass - verify input */
	size = size_block(&diff_a_head);
	for (x = 0; x != size; x++) {
		ch = peek_block(&diff_a_head, x) & 0xFF;
		if (!(ch & 0x80) && ch != '\t' && ch != '\r' && ch != '\n' &&
		    ch != ' ' && !isprint(ch))
			errx(EX_SOFTWARE, "Non printable characters are not allowed: '%c'", ch);
		else if (ch & 0x80) {
			set_block(&diff_a_head, x, ch | BLOCK_MASK);
		}
	}

	/* second pass - identify all comments */
	for (x = 0; x < size; x++) {
		ch = peek_block(&diff_a_head, x);
		chn = peek_block(&diff_a_head, x + 1);
		if ((ch & 0xFF) == '/' && (chn & 0xFF) == '/') {
			set_block(&diff_a_head, x, ch | BLOCK_MASK);
			set_block(&diff_a_head, x + 1, chn | BLOCK_MASK);
			for (x += 2; x < size; x++) {
				ch = peek_block(&diff_a_head, x);
				if ((ch & 0xFF) == '\n')
					break;
				set_block(&diff_a_head, x, ch | BLOCK_MASK);
			}
		} else if ((ch & 0xFF) == '/' && (chn & 0xFF) == '*') {
			set_block(&diff_a_head, x, ch | BLOCK_MASK);
			set_block(&diff_a_head, x + 1, chn | BLOCK_MASK);
			for (x += 2; x < size; x++) {
				ch = peek_block(&diff_a_head, x);
				chn = peek_block(&diff_a_head, x + 1);
				if ((ch & 0xFF) == '*' && (chn & 0xFF) == '/') {
					set_block(&diff_a_head, x, ch | BLOCK_MASK);
					set_block(&diff_a_head, x + 1, chn | BLOCK_MASK);
					x++;
					break;
				}
				set_block(&diff_a_head, x, ch | BLOCK_MASK);
			}
		}
	}

	/* third pass - identify preprocessor tokens and strings */
	for (x = 0; x < size; x++) {
		ch = peek_block(&diff_a_head, x);
		if (ch & BLOCK_MASK)
			continue;
		if (inside_string == 0 && (ch & 0xFF) == '#') {
			int skip_newline = 0;

			set_block(&diff_a_head, x, ch | BLOCK_MASK);
			for (x++; x < size; x++) {
				ch = peek_block(&diff_a_head, x);
				if ((ch & 0xFF) == '\n') {
					if (!skip_newline)
						break;
					skip_newline = 0;
				}
				if (ch & BLOCK_MASK)
					continue;
				if ((ch & 0xFF) == '\\')
					skip_newline = 1;
				set_block(&diff_a_head, x, ch | BLOCK_MASK);
			}
		}
		if ((ch & 0xFF) == '"' || (ch & 0xFF) == '\'') {
			if (inside_string == 0) {
				inside_string = (ch & 0xFF);
			} else {
				if (escape_char == 0 && inside_string == (ch & 0xFF))
					inside_string = 0;
			}
			escape_char = 0;
			set_block(&diff_a_head, x, ch | BLOCK_MASK);
		} else if (inside_string != 0) {
			if ((ch & 0xFF) == '\\')
				escape_char = !escape_char;
			else
				escape_char = 0;
			set_block(&diff_a_head, x, ch | BLOCK_MASK);
		}
	}

	/* fourth pass - identify function blocks */
	if (opt_verbose > 0) {
		chn = peek_block(&diff_a_head, x);
		printf("L%02d%c|", recurse,
		    (chn & BLOCK_ADD) ? '+' : ' ');
	}
	for (x = 0; x < size; x++) {
		ch = peek_block(&diff_a_head, x);
		if (opt_verbose > 0) {
			printf("%c", ch & 0xFF);
			if ((ch & 0xFF) == '\n') {
				chn = peek_block(&diff_a_head, x + 1);
				printf("L%02d%c|", recurse,
				    (chn & BLOCK_ADD) ? '+' : ' ');
			}
		}
		if (ch & BLOCK_MASK)
			continue;
		switch (ch & 0xFF) {
		case '{':
		case '(':
			recurse++;
			break;
		default:
			break;
		}
		if (recurse != 0)
			set_block(&diff_a_head, x, ch | BLOCK_MASK);
		switch (ch & 0xFF) {
		case '}':
		case ')':
			recurse--;
			break;
		default:
			break;
		}
	}
	if (opt_verbose > 0)
		printf("\n");
	if (recurse != 0)
		errx(EX_SOFTWARE, "Unbalanced parenthesis");
	if (inside_string != 0)
		errx(EX_SOFTWARE, "String without end");

	/* fifth pass - on the same line statements */
	for (x = 0; x < size; x++) {
		ch = peek_block(&diff_a_head, x);
		if (ch & BLOCK_MASK)
			continue;
		switch (ch & 0xFF) {
		case '\n':
			break;
		default:
			set_block(&diff_a_head, x, ch | BLOCK_MASK);
			break;
		}
	}

	/* sixth pass - output relevant blocks to indent */
	for (y1 = x = 0; x < size; x++) {
		ch = peek_block(&diff_a_head, x);
		if (ch & BLOCK_ADD) {
			TAILQ_INIT(&indent_in_head);

			p2 = alloc_block();
			y2 = 0;
			for (; y1 < size; y1++) {
				ch = peek_block(&diff_a_head, y1);
				if (y1 > x && !(ch & (BLOCK_MASK | BLOCK_ADD)))
					break;
				p2->data[y2++] = ch & 0xFF;
				if (y2 == BLOCK_SIZE) {
					TAILQ_INSERT_TAIL(&indent_in_head, p2, entry);
					p2 = alloc_block();
					y2 = 0;
				}
			}
			if (p2->data[y2] != '\n')
				p2->data[y2++] = '\n';
			p2->length = y2;
			TAILQ_INSERT_TAIL(&indent_in_head, p2, entry);

			cmd_block_process(&indent_in_head, &indent_out_head,
			    "indent "
			    "-Tbool "
			    "-Tclass "
			    "-TFILE "
			    "-TLIST_ENTRY "
			    "-TLIST_HEAD "
			    "-TSLIST_ENTRY "
			    "-TSLIST_HEAD "
			    "-TSTAILQ_ENTRY "
			    "-TSTAILQ_HEAD "
			    "-TTAILQ_ENTRY "
			    "-TTAILQ_HEAD "
			    "-T__aligned "
			    "-T__packed "
			    "-T__unused "
			    "-T__used "
			    "-Tfd_set "
			    "-Toss_mixerinfo "
			    "-Tu_char "
			    "-Tu_int "
			    "-Tu_long "
			    "-Tu_short "
			    "-ta -st -bad -bap -nbbb -nbc -br -nbs "
			    "-c41 -cd41 -cdb -ce -ci4 -cli0 -d0 -di8 -ndj -ei -nfc1 "
			    "-nfcb -i8 -ip8 -l79 -lc77 -ldi0 -nlp -npcs -psl -sc "
			    "-nsob -nv "
			    " | "
			    "sed "
			    "-e 's/_HEAD [(]/_HEAD(/g' "
			    "-e 's/_ENTRY [(]/_ENTRY(/g' "
			    "-e 's/\t__aligned/ __aligned/g' "
			    "-e 's/\t__packed/ __packed/g' "
			    "-e 's/\t__unused/ __unused/g' "
			    "-e 's/\t__used/ __used/g' "
			    "-e 's/^#define /#define\t/g'");

			if (opt_diff_tool != NULL) {
				if (diff_tool(&indent_in_head, &indent_out_head))
					retval = 1;
			} else {
				if (diff_block(&indent_in_head, &indent_out_head))
					retval = 1;
			}
			free_block(&indent_in_head);
			free_block(&indent_out_head);
			x = y1;
		} else if (!(ch & BLOCK_MASK)) {
			y1 = x + 1;
		}
	}
	return (retval);
}
