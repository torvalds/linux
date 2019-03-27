/*
 * Simple XZ decoder command line tool
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

/*
 * This is really limited: Not all filters from .xz format are supported,
 * only CRC32 is supported as the integrity check, and decoding of
 * concatenated .xz streams is not supported. Thus, you may want to look
 * at xzdec from XZ Utils if a few KiB bigger tool is not a problem.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "xz.h"

static uint8_t in[BUFSIZ];
static uint8_t out[BUFSIZ];

int main(int argc, char **argv)
{
	struct xz_buf b;
	struct xz_dec *s;
	enum xz_ret ret;
	const char *msg;

	if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
		fputs("Uncompress a .xz file from stdin to stdout.\n"
				"Arguments other than `--help' are ignored.\n",
				stdout);
		return 0;
	}

	xz_crc32_init();
#ifdef XZ_USE_CRC64
	xz_crc64_init();
#endif

	/*
	 * Support up to 64 MiB dictionary. The actually needed memory
	 * is allocated once the headers have been parsed.
	 */
	s = xz_dec_init(XZ_DYNALLOC, 1 << 26);
	if (s == NULL) {
		msg = "Memory allocation failed\n";
		goto error;
	}

	b.in = in;
	b.in_pos = 0;
	b.in_size = 0;
	b.out = out;
	b.out_pos = 0;
	b.out_size = BUFSIZ;

	while (true) {
		if (b.in_pos == b.in_size) {
			b.in_size = fread(in, 1, sizeof(in), stdin);
			b.in_pos = 0;
		}

		ret = xz_dec_run(s, &b);

		if (b.out_pos == sizeof(out)) {
			if (fwrite(out, 1, b.out_pos, stdout) != b.out_pos) {
				msg = "Write error\n";
				goto error;
			}

			b.out_pos = 0;
		}

		if (ret == XZ_OK)
			continue;

#ifdef XZ_DEC_ANY_CHECK
		if (ret == XZ_UNSUPPORTED_CHECK) {
			fputs(argv[0], stderr);
			fputs(": ", stderr);
			fputs("Unsupported check; not verifying "
					"file integrity\n", stderr);
			continue;
		}
#endif

		if (fwrite(out, 1, b.out_pos, stdout) != b.out_pos
				|| fclose(stdout)) {
			msg = "Write error\n";
			goto error;
		}

		switch (ret) {
		case XZ_STREAM_END:
			xz_dec_end(s);
			return 0;

		case XZ_MEM_ERROR:
			msg = "Memory allocation failed\n";
			goto error;

		case XZ_MEMLIMIT_ERROR:
			msg = "Memory usage limit reached\n";
			goto error;

		case XZ_FORMAT_ERROR:
			msg = "Not a .xz file\n";
			goto error;

		case XZ_OPTIONS_ERROR:
			msg = "Unsupported options in the .xz headers\n";
			goto error;

		case XZ_DATA_ERROR:
		case XZ_BUF_ERROR:
			msg = "File is corrupt\n";
			goto error;

		default:
			msg = "Bug!\n";
			goto error;
		}
	}

error:
	xz_dec_end(s);
	fputs(argv[0], stderr);
	fputs(": ", stderr);
	fputs(msg, stderr);
	return 1;
}
