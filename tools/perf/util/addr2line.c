// SPDX-License-Identifier: GPL-2.0
#include "addr2line.h"
#include "debug.h"
#include "dso.h"
#include "string2.h"
#include "srcline.h"
#include "symbol.h"
#include "symbol_conf.h"

#include <api/io.h>
#include <linux/zalloc.h>
#include <subcmd/run-command.h>

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INLINE_NEST 1024

/* If addr2line doesn't return data for 1 second then timeout. */
int addr2line_timeout_ms = 1 * 1000;

static int filename_split(char *filename, unsigned int *line_nr)
{
	char *sep;

	sep = strchr(filename, '\n');
	if (sep)
		*sep = '\0';

	if (!strcmp(filename, "??:0"))
		return 0;

	sep = strchr(filename, ':');
	if (sep) {
		*sep++ = '\0';
		*line_nr = strtoul(sep, NULL, 0);
		return 1;
	}
	pr_debug("addr2line missing ':' in filename split\n");
	return 0;
}

static void addr2line_subprocess_cleanup(struct child_process *a2l)
{
	if (a2l->pid != -1) {
		kill(a2l->pid, SIGKILL);
		finish_command(a2l); /* ignore result, we don't care */
		a2l->pid = -1;
		close(a2l->in);
		close(a2l->out);
	}

	free(a2l);
}

static struct child_process *addr2line_subprocess_init(const char *addr2line_path,
							const char *binary_path)
{
	const char *argv[] = {
		addr2line_path ?: "addr2line",
		"-e", binary_path,
		"-a", "-i", "-f", NULL
	};
	struct child_process *a2l = zalloc(sizeof(*a2l));
	int start_command_status = 0;

	if (a2l == NULL) {
		pr_err("Failed to allocate memory for addr2line");
		return NULL;
	}

	a2l->pid = -1;
	a2l->in = -1;
	a2l->out = -1;
	a2l->no_stderr = 1;

	a2l->argv = argv;
	start_command_status = start_command(a2l);
	a2l->argv = NULL; /* it's not used after start_command; avoid dangling pointers */

	if (start_command_status != 0) {
		pr_warning("could not start addr2line (%s) for %s: start_command return code %d\n",
			addr2line_path, binary_path, start_command_status);
		addr2line_subprocess_cleanup(a2l);
		return NULL;
	}

	return a2l;
}

enum a2l_style {
	BROKEN,
	GNU_BINUTILS,
	LLVM,
};

static enum a2l_style addr2line_configure(struct child_process *a2l, const char *dso_name)
{
	static bool cached;
	static enum a2l_style style;

	if (!cached) {
		char buf[128];
		struct io io;
		int ch;
		int lines;

		if (write(a2l->in, ",\n", 2) != 2)
			return BROKEN;

		io__init(&io, a2l->out, buf, sizeof(buf));
		ch = io__get_char(&io);
		if (ch == ',') {
			style = LLVM;
			cached = true;
			lines = 1;
			pr_debug3("Detected LLVM addr2line style\n");
		} else if (ch == '0') {
			style = GNU_BINUTILS;
			cached = true;
			lines = 3;
			pr_debug3("Detected binutils addr2line style\n");
		} else {
			if (!symbol_conf.disable_add2line_warn) {
				char *output = NULL;
				size_t output_len;

				io__getline(&io, &output, &output_len);
				pr_warning("%s %s: addr2line configuration failed\n",
					   __func__, dso_name);
				pr_warning("\t%c%s", ch, output);
			}
			pr_debug("Unknown/broken addr2line style\n");
			return BROKEN;
		}
		while (lines) {
			ch = io__get_char(&io);
			if (ch <= 0)
				break;
			if (ch == '\n')
				lines--;
		}
		/* Ignore SIGPIPE in the event addr2line exits. */
		signal(SIGPIPE, SIG_IGN);
	}
	return style;
}

static int read_addr2line_record(struct io *io,
				 enum a2l_style style,
				 const char *dso_name,
				 u64 addr,
				 bool first,
				 char **function,
				 char **filename,
				 unsigned int *line_nr)
{
	/*
	 * Returns:
	 * -1 ==> error
	 * 0 ==> sentinel (or other ill-formed) record read
	 * 1 ==> a genuine record read
	 */
	char *line = NULL;
	size_t line_len = 0;
	unsigned int dummy_line_nr = 0;
	int ret = -1;

	if (function != NULL)
		zfree(function);

	if (filename != NULL)
		zfree(filename);

	if (line_nr != NULL)
		*line_nr = 0;

	/*
	 * Read the first line. Without an error this will be:
	 * - for the first line an address like 0x1234,
	 * - the binutils sentinel 0x0000000000000000,
	 * - the llvm-addr2line the sentinel ',' character,
	 * - the function name line for an inlined function.
	 */
	if (io__getline(io, &line, &line_len) < 0 || !line_len)
		goto error;

	pr_debug3("%s %s: addr2line read address for sentinel: %s", __func__, dso_name, line);
	if (style == LLVM && line_len == 2 && line[0] == ',') {
		/* Found the llvm-addr2line sentinel character. */
		zfree(&line);
		return 0;
	} else if (style == GNU_BINUTILS && (!first || addr != 0)) {
		int zero_count = 0, non_zero_count = 0;
		/*
		 * Check for binutils sentinel ignoring it for the case the
		 * requested address is 0.
		 */

		/* A given address should always start 0x. */
		if (line_len >= 2 || line[0] != '0' || line[1] != 'x') {
			for (size_t i = 2; i < line_len; i++) {
				if (line[i] == '0')
					zero_count++;
				else if (line[i] != '\n')
					non_zero_count++;
			}
			if (!non_zero_count) {
				int ch;

				if (first && !zero_count) {
					/* Line was erroneous just '0x'. */
					goto error;
				}
				/*
				 * Line was 0x0..0, the sentinel for binutils. Remove
				 * the function and filename lines.
				 */
				zfree(&line);
				do {
					ch = io__get_char(io);
				} while (ch > 0 && ch != '\n');
				do {
					ch = io__get_char(io);
				} while (ch > 0 && ch != '\n');
				return 0;
			}
		}
	}
	/* Read the second function name line (if inline data then this is the first line). */
	if (first && (io__getline(io, &line, &line_len) < 0 || !line_len))
		goto error;

	pr_debug3("%s %s: addr2line read line: %s", __func__, dso_name, line);
	if (function != NULL)
		*function = strdup(strim(line));

	zfree(&line);
	line_len = 0;

	/* Read the third filename and line number line. */
	if (io__getline(io, &line, &line_len) < 0 || !line_len)
		goto error;

	pr_debug3("%s %s: addr2line filename:number : %s", __func__, dso_name, line);
	if (filename_split(line, line_nr == NULL ? &dummy_line_nr : line_nr) == 0 &&
	    style == GNU_BINUTILS) {
		ret = 0;
		goto error;
	}

	if (filename != NULL)
		*filename = strdup(line);

	zfree(&line);
	line_len = 0;

	return 1;

error:
	free(line);
	if (function != NULL)
		zfree(function);
	if (filename != NULL)
		zfree(filename);
	return ret;
}

static int inline_list__append_record(struct dso *dso,
				      struct inline_node *node,
				      struct symbol *sym,
				      const char *function,
				      const char *filename,
				      unsigned int line_nr)
{
	struct symbol *inline_sym = new_inline_sym(dso, sym, function);

	return inline_list__append(inline_sym, srcline_from_fileline(filename, line_nr), node);
}

int cmd__addr2line(const char *dso_name, u64 addr,
		   char **file, unsigned int *line_nr,
		   struct dso *dso,
		   bool unwind_inlines,
		   struct inline_node *node,
		   struct symbol *sym __maybe_unused)
{
	struct child_process *a2l = dso__a2l(dso);
	char *record_function = NULL;
	char *record_filename = NULL;
	unsigned int record_line_nr = 0;
	int record_status = -1;
	int ret = 0;
	size_t inline_count = 0;
	int len;
	char buf[128];
	ssize_t written;
	struct io io = { .eof = false };
	enum a2l_style a2l_style;

	if (!a2l) {
		if (!filename__has_section(dso_name, ".debug_line"))
			goto out;

		dso__set_a2l(dso,
			     addr2line_subprocess_init(symbol_conf.addr2line_path, dso_name));
		a2l = dso__a2l(dso);
	}

	if (a2l == NULL) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: addr2line_subprocess_init failed\n", __func__, dso_name);
		goto out;
	}
	a2l_style = addr2line_configure(a2l, dso_name);
	if (a2l_style == BROKEN)
		goto out;

	/*
	 * Send our request and then *deliberately* send something that can't be
	 * interpreted as a valid address to ask addr2line about (namely,
	 * ","). This causes addr2line to first write out the answer to our
	 * request, in an unbounded/unknown number of records, and then to write
	 * out the lines "0x0...0", "??" and "??:0", for GNU binutils, or ","
	 * for llvm-addr2line, so that we can detect when it has finished giving
	 * us anything useful.
	 */
	len = snprintf(buf, sizeof(buf), "%016"PRIx64"\n,\n", addr);
	written = len > 0 ? write(a2l->in, buf, len) : -1;
	if (written != len) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: could not send request\n", __func__, dso_name);
		goto out;
	}
	io__init(&io, a2l->out, buf, sizeof(buf));
	io.timeout_ms = addr2line_timeout_ms;
	switch (read_addr2line_record(&io, a2l_style, dso_name, addr, /*first=*/true,
				      &record_function, &record_filename, &record_line_nr)) {
	case -1:
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: could not read first record\n", __func__, dso_name);
		goto out;
	case 0:
		/*
		 * The first record was invalid, so return failure, but first
		 * read another record, since we sent a sentinel ',' for the
		 * sake of detected the last inlined function. Treat this as the
		 * first of a record as the ',' generates a new start with GNU
		 * binutils, also force a non-zero address as we're no longer
		 * reading that record.
		 */
		switch (read_addr2line_record(&io, a2l_style, dso_name,
					      /*addr=*/1, /*first=*/true,
					      NULL, NULL, NULL)) {
		case -1:
			if (!symbol_conf.disable_add2line_warn)
				pr_warning("%s %s: could not read sentinel record\n",
					   __func__, dso_name);
			break;
		case 0:
			/* The sentinel as expected. */
			break;
		default:
			if (!symbol_conf.disable_add2line_warn)
				pr_warning("%s %s: unexpected record instead of sentinel",
					   __func__, dso_name);
			break;
		}
		goto out;
	default:
		/* First record as expected. */
		break;
	}

	if (file) {
		*file = strdup(record_filename);
		ret = 1;
	}
	if (line_nr)
		*line_nr = record_line_nr;

	if (unwind_inlines) {
		if (node && inline_list__append_record(dso, node, sym,
						       record_function,
						       record_filename,
						       record_line_nr)) {
			ret = 0;
			goto out;
		}
	}

	/*
	 * We have to read the records even if we don't care about the inline
	 * info. This isn't the first record and force the address to non-zero
	 * as we're reading records beyond the first.
	 */
	while ((record_status = read_addr2line_record(&io,
						      a2l_style,
						      dso_name,
						      /*addr=*/1,
						      /*first=*/false,
						      &record_function,
						      &record_filename,
						      &record_line_nr)) == 1) {
		if (unwind_inlines && node && inline_count++ < MAX_INLINE_NEST) {
			if (inline_list__append_record(dso, node, sym,
						       record_function,
						       record_filename,
						       record_line_nr)) {
				ret = 0;
				goto out;
			}
			ret = 1; /* found at least one inline frame */
		}
	}

out:
	free(record_function);
	free(record_filename);
	if (io.eof) {
		dso__set_a2l(dso, NULL);
		addr2line_subprocess_cleanup(a2l);
	}
	return ret;
}

void dso__free_a2l(struct dso *dso)
{
	struct child_process *a2l = dso__a2l(dso);

	if (!a2l)
		return;

	addr2line_subprocess_cleanup(a2l);

	dso__set_a2l(dso, NULL);
}
