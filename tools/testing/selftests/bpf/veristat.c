// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#define _GNU_SOURCE
#include <argp.h>
#include <string.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <sched.h>
#include <pthread.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <bpf/libbpf.h>
#include <libelf.h>
#include <gelf.h>

enum stat_id {
	VERDICT,
	DURATION,
	TOTAL_INSNS,
	TOTAL_STATES,
	PEAK_STATES,
	MAX_STATES_PER_INSN,
	MARK_READ_MAX_LEN,

	FILE_NAME,
	PROG_NAME,

	ALL_STATS_CNT,
	NUM_STATS_CNT = FILE_NAME - VERDICT,
};

struct verif_stats {
	char *file_name;
	char *prog_name;

	long stats[NUM_STATS_CNT];
};

struct stat_specs {
	int spec_cnt;
	enum stat_id ids[ALL_STATS_CNT];
	bool asc[ALL_STATS_CNT];
	int lens[ALL_STATS_CNT * 3]; /* 3x for comparison mode */
};

enum resfmt {
	RESFMT_TABLE,
	RESFMT_TABLE_CALCLEN, /* fake format to pre-calculate table's column widths */
	RESFMT_CSV,
};

struct filter {
	char *file_glob;
	char *prog_glob;
};

static struct env {
	char **filenames;
	int filename_cnt;
	bool verbose;
	bool quiet;
	int log_level;
	enum resfmt out_fmt;
	bool comparison_mode;

	struct verif_stats *prog_stats;
	int prog_stat_cnt;

	/* baseline_stats is allocated and used only in comparsion mode */
	struct verif_stats *baseline_stats;
	int baseline_stat_cnt;

	struct stat_specs output_spec;
	struct stat_specs sort_spec;

	struct filter *allow_filters;
	struct filter *deny_filters;
	int allow_filter_cnt;
	int deny_filter_cnt;

	int files_processed;
	int files_skipped;
	int progs_processed;
	int progs_skipped;
} env;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (!env.verbose)
		return 0;
	if (level == LIBBPF_DEBUG /* && !env.verbose */)
		return 0;
	return vfprintf(stderr, format, args);
}

const char *argp_program_version = "veristat";
const char *argp_program_bug_address = "<bpf@vger.kernel.org>";
const char argp_program_doc[] =
"veristat    BPF verifier stats collection and comparison tool.\n"
"\n"
"USAGE: veristat <obj-file> [<obj-file>...]\n"
"   OR: veristat -C <baseline.csv> <comparison.csv>\n";

static const struct argp_option opts[] = {
	{ NULL, 'h', NULL, OPTION_HIDDEN, "Show the full help" },
	{ "verbose", 'v', NULL, 0, "Verbose mode" },
	{ "log-level", 'l', "LEVEL", 0, "Verifier log level (default 0 for normal mode, 1 for verbose mode)" },
	{ "quiet", 'q', NULL, 0, "Quiet mode" },
	{ "emit", 'e', "SPEC", 0, "Specify stats to be emitted" },
	{ "sort", 's', "SPEC", 0, "Specify sort order" },
	{ "output-format", 'o', "FMT", 0, "Result output format (table, csv), default is table." },
	{ "compare", 'C', NULL, 0, "Comparison mode" },
	{ "filter", 'f', "FILTER", 0, "Filter expressions (or @filename for file with expressions)." },
	{},
};

static int parse_stats(const char *stats_str, struct stat_specs *specs);
static int append_filter(struct filter **filters, int *cnt, const char *str);
static int append_filter_file(const char *path);

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	void *tmp;
	int err;

	switch (key) {
	case 'h':
		argp_state_help(state, stderr, ARGP_HELP_STD_HELP);
		break;
	case 'v':
		env.verbose = true;
		break;
	case 'q':
		env.quiet = true;
		break;
	case 'e':
		err = parse_stats(arg, &env.output_spec);
		if (err)
			return err;
		break;
	case 's':
		err = parse_stats(arg, &env.sort_spec);
		if (err)
			return err;
		break;
	case 'o':
		if (strcmp(arg, "table") == 0) {
			env.out_fmt = RESFMT_TABLE;
		} else if (strcmp(arg, "csv") == 0) {
			env.out_fmt = RESFMT_CSV;
		} else {
			fprintf(stderr, "Unrecognized output format '%s'\n", arg);
			return -EINVAL;
		}
		break;
	case 'l':
		errno = 0;
		env.log_level = strtol(arg, NULL, 10);
		if (errno) {
			fprintf(stderr, "invalid log level: %s\n", arg);
			argp_usage(state);
		}
		break;
	case 'C':
		env.comparison_mode = true;
		break;
	case 'f':
		if (arg[0] == '@')
			err = append_filter_file(arg + 1);
		else if (arg[0] == '!')
			err = append_filter(&env.deny_filters, &env.deny_filter_cnt, arg + 1);
		else
			err = append_filter(&env.allow_filters, &env.allow_filter_cnt, arg);
		if (err) {
			fprintf(stderr, "Failed to collect program filter expressions: %d\n", err);
			return err;
		}
		break;
	case ARGP_KEY_ARG:
		tmp = realloc(env.filenames, (env.filename_cnt + 1) * sizeof(*env.filenames));
		if (!tmp)
			return -ENOMEM;
		env.filenames = tmp;
		env.filenames[env.filename_cnt] = strdup(arg);
		if (!env.filenames[env.filename_cnt])
			return -ENOMEM;
		env.filename_cnt++;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static const struct argp argp = {
	.options = opts,
	.parser = parse_arg,
	.doc = argp_program_doc,
};


/* Adapted from perf/util/string.c */
static bool glob_matches(const char *str, const char *pat)
{
	while (*str && *pat && *pat != '*') {
		if (*str != *pat)
			return false;
		str++;
		pat++;
	}
	/* Check wild card */
	if (*pat == '*') {
		while (*pat == '*')
			pat++;
		if (!*pat) /* Tail wild card matches all */
			return true;
		while (*str)
			if (glob_matches(str++, pat))
				return true;
	}
	return !*str && !*pat;
}

static bool should_process_file(const char *filename)
{
	int i;

	if (env.deny_filter_cnt > 0) {
		for (i = 0; i < env.deny_filter_cnt; i++) {
			if (glob_matches(filename, env.deny_filters[i].file_glob))
				return false;
		}
	}

	if (env.allow_filter_cnt == 0)
		return true;

	for (i = 0; i < env.allow_filter_cnt; i++) {
		if (glob_matches(filename, env.allow_filters[i].file_glob))
			return true;
	}

	return false;
}

static bool is_bpf_obj_file(const char *path) {
	Elf64_Ehdr *ehdr;
	int fd, err = -EINVAL;
	Elf *elf = NULL;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return true; /* we'll fail later and propagate error */

	/* ensure libelf is initialized */
	(void)elf_version(EV_CURRENT);

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (!elf)
		goto cleanup;

	if (elf_kind(elf) != ELF_K_ELF || gelf_getclass(elf) != ELFCLASS64)
		goto cleanup;

	ehdr = elf64_getehdr(elf);
	/* Old LLVM set e_machine to EM_NONE */
	if (!ehdr || ehdr->e_type != ET_REL || (ehdr->e_machine && ehdr->e_machine != EM_BPF))
		goto cleanup;

	err = 0;
cleanup:
	if (elf)
		elf_end(elf);
	close(fd);
	return err == 0;
}

static bool should_process_prog(const char *path, const char *prog_name)
{
	const char *filename = basename(path);
	int i;

	if (env.deny_filter_cnt > 0) {
		for (i = 0; i < env.deny_filter_cnt; i++) {
			if (glob_matches(filename, env.deny_filters[i].file_glob))
				return false;
			if (!env.deny_filters[i].prog_glob)
				continue;
			if (glob_matches(prog_name, env.deny_filters[i].prog_glob))
				return false;
		}
	}

	if (env.allow_filter_cnt == 0)
		return true;

	for (i = 0; i < env.allow_filter_cnt; i++) {
		if (!glob_matches(filename, env.allow_filters[i].file_glob))
			continue;
		/* if filter specifies only filename glob part, it implicitly
		 * allows all progs within that file
		 */
		if (!env.allow_filters[i].prog_glob)
			return true;
		if (glob_matches(prog_name, env.allow_filters[i].prog_glob))
			return true;
	}

	return false;
}

static int append_filter(struct filter **filters, int *cnt, const char *str)
{
	struct filter *f;
	void *tmp;
	const char *p;

	tmp = realloc(*filters, (*cnt + 1) * sizeof(**filters));
	if (!tmp)
		return -ENOMEM;
	*filters = tmp;

	f = &(*filters)[*cnt];
	f->file_glob = f->prog_glob = NULL;

	/* filter can be specified either as "<obj-glob>" or "<obj-glob>/<prog-glob>" */
	p = strchr(str, '/');
	if (!p) {
		f->file_glob = strdup(str);
		if (!f->file_glob)
			return -ENOMEM;
	} else {
		f->file_glob = strndup(str, p - str);
		f->prog_glob = strdup(p + 1);
		if (!f->file_glob || !f->prog_glob) {
			free(f->file_glob);
			free(f->prog_glob);
			f->file_glob = f->prog_glob = NULL;
			return -ENOMEM;
		}
	}

	*cnt = *cnt + 1;
	return 0;
}

static int append_filter_file(const char *path)
{
	char buf[1024];
	FILE *f;
	int err = 0;

	f = fopen(path, "r");
	if (!f) {
		err = -errno;
		fprintf(stderr, "Failed to open filters in '%s': %d\n", path, err);
		return err;
	}

	while (fscanf(f, " %1023[^\n]\n", buf) == 1) {
		/* lines starting with # are comments, skip them */
		if (buf[0] == '\0' || buf[0] == '#')
			continue;
		/* lines starting with ! are negative match filters */
		if (buf[0] == '!')
			err = append_filter(&env.deny_filters, &env.deny_filter_cnt, buf + 1);
		else
			err = append_filter(&env.allow_filters, &env.allow_filter_cnt, buf);
		if (err)
			goto cleanup;
	}

cleanup:
	fclose(f);
	return err;
}

static const struct stat_specs default_output_spec = {
	.spec_cnt = 7,
	.ids = {
		FILE_NAME, PROG_NAME, VERDICT, DURATION,
		TOTAL_INSNS, TOTAL_STATES, PEAK_STATES,
	},
};

static const struct stat_specs default_sort_spec = {
	.spec_cnt = 2,
	.ids = {
		FILE_NAME, PROG_NAME,
	},
	.asc = { true, true, },
};

static struct stat_def {
	const char *header;
	const char *names[4];
	bool asc_by_default;
} stat_defs[] = {
	[FILE_NAME] = { "File", {"file_name", "filename", "file"}, true /* asc */ },
	[PROG_NAME] = { "Program", {"prog_name", "progname", "prog"}, true /* asc */ },
	[VERDICT] = { "Verdict", {"verdict"}, true /* asc: failure, success */ },
	[DURATION] = { "Duration (us)", {"duration", "dur"}, },
	[TOTAL_INSNS] = { "Total insns", {"total_insns", "insns"}, },
	[TOTAL_STATES] = { "Total states", {"total_states", "states"}, },
	[PEAK_STATES] = { "Peak states", {"peak_states"}, },
	[MAX_STATES_PER_INSN] = { "Max states per insn", {"max_states_per_insn"}, },
	[MARK_READ_MAX_LEN] = { "Max mark read length", {"max_mark_read_len", "mark_read"}, },
};

static int parse_stat(const char *stat_name, struct stat_specs *specs)
{
	int id, i;

	if (specs->spec_cnt >= ARRAY_SIZE(specs->ids)) {
		fprintf(stderr, "Can't specify more than %zd stats\n", ARRAY_SIZE(specs->ids));
		return -E2BIG;
	}

	for (id = 0; id < ARRAY_SIZE(stat_defs); id++) {
		struct stat_def *def = &stat_defs[id];

		for (i = 0; i < ARRAY_SIZE(stat_defs[id].names); i++) {
			if (!def->names[i] || strcmp(def->names[i], stat_name) != 0)
				continue;

			specs->ids[specs->spec_cnt] = id;
			specs->asc[specs->spec_cnt] = def->asc_by_default;
			specs->spec_cnt++;

			return 0;
		}
	}

	fprintf(stderr, "Unrecognized stat name '%s'\n", stat_name);
	return -ESRCH;
}

static int parse_stats(const char *stats_str, struct stat_specs *specs)
{
	char *input, *state = NULL, *next;
	int err;

	input = strdup(stats_str);
	if (!input)
		return -ENOMEM;

	while ((next = strtok_r(state ? NULL : input, ",", &state))) {
		err = parse_stat(next, specs);
		if (err)
			return err;
	}

	return 0;
}

static void free_verif_stats(struct verif_stats *stats, size_t stat_cnt)
{
	int i;

	if (!stats)
		return;

	for (i = 0; i < stat_cnt; i++) {
		free(stats[i].file_name);
		free(stats[i].prog_name);
	}
	free(stats);
}

static char verif_log_buf[64 * 1024];

#define MAX_PARSED_LOG_LINES 100

static int parse_verif_log(char * const buf, size_t buf_sz, struct verif_stats *s)
{
	const char *cur;
	int pos, lines;

	buf[buf_sz - 1] = '\0';

	for (pos = strlen(buf) - 1, lines = 0; pos >= 0 && lines < MAX_PARSED_LOG_LINES; lines++) {
		/* find previous endline or otherwise take the start of log buf */
		for (cur = &buf[pos]; cur > buf && cur[0] != '\n'; cur--, pos--) {
		}
		/* next time start from end of previous line (or pos goes to <0) */
		pos--;
		/* if we found endline, point right after endline symbol;
		 * otherwise, stay at the beginning of log buf
		 */
		if (cur[0] == '\n')
			cur++;

		if (1 == sscanf(cur, "verification time %ld usec\n", &s->stats[DURATION]))
			continue;
		if (6 == sscanf(cur, "processed %ld insns (limit %*d) max_states_per_insn %ld total_states %ld peak_states %ld mark_read %ld",
				&s->stats[TOTAL_INSNS],
				&s->stats[MAX_STATES_PER_INSN],
				&s->stats[TOTAL_STATES],
				&s->stats[PEAK_STATES],
				&s->stats[MARK_READ_MAX_LEN]))
			continue;
	}

	return 0;
}

static int process_prog(const char *filename, struct bpf_object *obj, struct bpf_program *prog)
{
	const char *prog_name = bpf_program__name(prog);
	size_t buf_sz = sizeof(verif_log_buf);
	char *buf = verif_log_buf;
	struct verif_stats *stats;
	int err = 0;
	void *tmp;

	if (!should_process_prog(filename, bpf_program__name(prog))) {
		env.progs_skipped++;
		return 0;
	}

	tmp = realloc(env.prog_stats, (env.prog_stat_cnt + 1) * sizeof(*env.prog_stats));
	if (!tmp)
		return -ENOMEM;
	env.prog_stats = tmp;
	stats = &env.prog_stats[env.prog_stat_cnt++];
	memset(stats, 0, sizeof(*stats));

	if (env.verbose) {
		buf_sz = 16 * 1024 * 1024;
		buf = malloc(buf_sz);
		if (!buf)
			return -ENOMEM;
		bpf_program__set_log_buf(prog, buf, buf_sz);
		bpf_program__set_log_level(prog, env.log_level | 4); /* stats + log */
	} else {
		bpf_program__set_log_buf(prog, buf, buf_sz);
		bpf_program__set_log_level(prog, 4); /* only verifier stats */
	}
	verif_log_buf[0] = '\0';

	err = bpf_object__load(obj);
	env.progs_processed++;

	stats->file_name = strdup(basename(filename));
	stats->prog_name = strdup(bpf_program__name(prog));
	stats->stats[VERDICT] = err == 0; /* 1 - success, 0 - failure */
	parse_verif_log(buf, buf_sz, stats);

	if (env.verbose) {
		printf("PROCESSING %s/%s, DURATION US: %ld, VERDICT: %s, VERIFIER LOG:\n%s\n",
		       filename, prog_name, stats->stats[DURATION],
		       err ? "failure" : "success", buf);
	}

	if (verif_log_buf != buf)
		free(buf);

	return 0;
};

static int process_obj(const char *filename)
{
	struct bpf_object *obj = NULL, *tobj;
	struct bpf_program *prog, *tprog, *lprog;
	libbpf_print_fn_t old_libbpf_print_fn;
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	int err = 0, prog_cnt = 0;

	if (!should_process_file(basename(filename))) {
		if (env.verbose)
			printf("Skipping '%s' due to filters...\n", filename);
		env.files_skipped++;
		return 0;
	}
	if (!is_bpf_obj_file(filename)) {
		if (env.verbose)
			printf("Skipping '%s' as it's not a BPF object file...\n", filename);
		env.files_skipped++;
		return 0;
	}

	if (!env.quiet && env.out_fmt == RESFMT_TABLE)
		printf("Processing '%s'...\n", basename(filename));

	old_libbpf_print_fn = libbpf_set_print(libbpf_print_fn);
	obj = bpf_object__open_file(filename, &opts);
	if (!obj) {
		/* if libbpf can't open BPF object file, it could be because
		 * that BPF object file is incomplete and has to be statically
		 * linked into a final BPF object file; instead of bailing
		 * out, report it into stderr, mark it as skipped, and
		 * proceeed
		 */
		fprintf(stderr, "Failed to open '%s': %d\n", filename, -errno);
		env.files_skipped++;
		err = 0;
		goto cleanup;
	}

	env.files_processed++;

	bpf_object__for_each_program(prog, obj) {
		prog_cnt++;
	}

	if (prog_cnt == 1) {
		prog = bpf_object__next_program(obj, NULL);
		bpf_program__set_autoload(prog, true);
		process_prog(filename, obj, prog);
		goto cleanup;
	}

	bpf_object__for_each_program(prog, obj) {
		const char *prog_name = bpf_program__name(prog);

		tobj = bpf_object__open_file(filename, &opts);
		if (!tobj) {
			err = -errno;
			fprintf(stderr, "Failed to open '%s': %d\n", filename, err);
			goto cleanup;
		}

		bpf_object__for_each_program(tprog, tobj) {
			const char *tprog_name = bpf_program__name(tprog);

			if (strcmp(prog_name, tprog_name) == 0) {
				bpf_program__set_autoload(tprog, true);
				lprog = tprog;
			} else {
				bpf_program__set_autoload(tprog, false);
			}
		}

		process_prog(filename, tobj, lprog);
		bpf_object__close(tobj);
	}

cleanup:
	bpf_object__close(obj);
	libbpf_set_print(old_libbpf_print_fn);
	return err;
}

static int cmp_stat(const struct verif_stats *s1, const struct verif_stats *s2,
		    enum stat_id id, bool asc)
{
	int cmp = 0;

	switch (id) {
	case FILE_NAME:
		cmp = strcmp(s1->file_name, s2->file_name);
		break;
	case PROG_NAME:
		cmp = strcmp(s1->prog_name, s2->prog_name);
		break;
	case VERDICT:
	case DURATION:
	case TOTAL_INSNS:
	case TOTAL_STATES:
	case PEAK_STATES:
	case MAX_STATES_PER_INSN:
	case MARK_READ_MAX_LEN: {
		long v1 = s1->stats[id];
		long v2 = s2->stats[id];

		if (v1 != v2)
			cmp = v1 < v2 ? -1 : 1;
		break;
	}
	default:
		fprintf(stderr, "Unrecognized stat #%d\n", id);
		exit(1);
	}

	return asc ? cmp : -cmp;
}

static int cmp_prog_stats(const void *v1, const void *v2)
{
	const struct verif_stats *s1 = v1, *s2 = v2;
	int i, cmp;

	for (i = 0; i < env.sort_spec.spec_cnt; i++) {
		cmp = cmp_stat(s1, s2, env.sort_spec.ids[i], env.sort_spec.asc[i]);
		if (cmp != 0)
			return cmp;
	}

	return 0;
}

#define HEADER_CHAR '-'
#define COLUMN_SEP "  "

static void output_header_underlines(void)
{
	int i, j, len;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		len = env.output_spec.lens[i];

		printf("%s", i == 0 ? "" : COLUMN_SEP);
		for (j = 0; j < len; j++)
			printf("%c", HEADER_CHAR);
	}
	printf("\n");
}

static void output_headers(enum resfmt fmt)
{
	int i, len;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		int id = env.output_spec.ids[i];
		int *max_len = &env.output_spec.lens[i];

		switch (fmt) {
		case RESFMT_TABLE_CALCLEN:
			len = snprintf(NULL, 0, "%s", stat_defs[id].header);
			if (len > *max_len)
				*max_len = len;
			break;
		case RESFMT_TABLE:
			printf("%s%-*s", i == 0 ? "" : COLUMN_SEP,  *max_len, stat_defs[id].header);
			if (i == env.output_spec.spec_cnt - 1)
				printf("\n");
			break;
		case RESFMT_CSV:
			printf("%s%s", i == 0 ? "" : ",", stat_defs[id].names[0]);
			if (i == env.output_spec.spec_cnt - 1)
				printf("\n");
			break;
		}
	}

	if (fmt == RESFMT_TABLE)
		output_header_underlines();
}

static void prepare_value(const struct verif_stats *s, enum stat_id id,
			  const char **str, long *val)
{
	switch (id) {
	case FILE_NAME:
		*str = s->file_name;
		break;
	case PROG_NAME:
		*str = s->prog_name;
		break;
	case VERDICT:
		*str = s->stats[VERDICT] ? "success" : "failure";
		break;
	case DURATION:
	case TOTAL_INSNS:
	case TOTAL_STATES:
	case PEAK_STATES:
	case MAX_STATES_PER_INSN:
	case MARK_READ_MAX_LEN:
		*val = s->stats[id];
		break;
	default:
		fprintf(stderr, "Unrecognized stat #%d\n", id);
		exit(1);
	}
}

static void output_stats(const struct verif_stats *s, enum resfmt fmt, bool last)
{
	int i;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		int id = env.output_spec.ids[i];
		int *max_len = &env.output_spec.lens[i], len;
		const char *str = NULL;
		long val = 0;

		prepare_value(s, id, &str, &val);

		switch (fmt) {
		case RESFMT_TABLE_CALCLEN:
			if (str)
				len = snprintf(NULL, 0, "%s", str);
			else
				len = snprintf(NULL, 0, "%ld", val);
			if (len > *max_len)
				*max_len = len;
			break;
		case RESFMT_TABLE:
			if (str)
				printf("%s%-*s", i == 0 ? "" : COLUMN_SEP, *max_len, str);
			else
				printf("%s%*ld", i == 0 ? "" : COLUMN_SEP,  *max_len, val);
			if (i == env.output_spec.spec_cnt - 1)
				printf("\n");
			break;
		case RESFMT_CSV:
			if (str)
				printf("%s%s", i == 0 ? "" : ",", str);
			else
				printf("%s%ld", i == 0 ? "" : ",", val);
			if (i == env.output_spec.spec_cnt - 1)
				printf("\n");
			break;
		}
	}

	if (last && fmt == RESFMT_TABLE) {
		output_header_underlines();
		printf("Done. Processed %d files, %d programs. Skipped %d files, %d programs.\n",
		       env.files_processed, env.files_skipped, env.progs_processed, env.progs_skipped);
	}
}

static int handle_verif_mode(void)
{
	int i, err;

	if (env.filename_cnt == 0) {
		fprintf(stderr, "Please provide path to BPF object file!\n");
		argp_help(&argp, stderr, ARGP_HELP_USAGE, "veristat");
		return -EINVAL;
	}

	for (i = 0; i < env.filename_cnt; i++) {
		err = process_obj(env.filenames[i]);
		if (err) {
			fprintf(stderr, "Failed to process '%s': %d\n", env.filenames[i], err);
			return err;
		}
	}

	qsort(env.prog_stats, env.prog_stat_cnt, sizeof(*env.prog_stats), cmp_prog_stats);

	if (env.out_fmt == RESFMT_TABLE) {
		/* calculate column widths */
		output_headers(RESFMT_TABLE_CALCLEN);
		for (i = 0; i < env.prog_stat_cnt; i++)
			output_stats(&env.prog_stats[i], RESFMT_TABLE_CALCLEN, false);
	}

	/* actually output the table */
	output_headers(env.out_fmt);
	for (i = 0; i < env.prog_stat_cnt; i++) {
		output_stats(&env.prog_stats[i], env.out_fmt, i == env.prog_stat_cnt - 1);
	}

	return 0;
}

static int parse_stat_value(const char *str, enum stat_id id, struct verif_stats *st)
{
	switch (id) {
	case FILE_NAME:
		st->file_name = strdup(str);
		if (!st->file_name)
			return -ENOMEM;
		break;
	case PROG_NAME:
		st->prog_name = strdup(str);
		if (!st->prog_name)
			return -ENOMEM;
		break;
	case VERDICT:
		if (strcmp(str, "success") == 0) {
			st->stats[VERDICT] = true;
		} else if (strcmp(str, "failure") == 0) {
			st->stats[VERDICT] = false;
		} else {
			fprintf(stderr, "Unrecognized verification verdict '%s'\n", str);
			return -EINVAL;
		}
		break;
	case DURATION:
	case TOTAL_INSNS:
	case TOTAL_STATES:
	case PEAK_STATES:
	case MAX_STATES_PER_INSN:
	case MARK_READ_MAX_LEN: {
		long val;
		int err, n;

		if (sscanf(str, "%ld %n", &val, &n) != 1 || n != strlen(str)) {
			err = -errno;
			fprintf(stderr, "Failed to parse '%s' as integer\n", str);
			return err;
		}

		st->stats[id] = val;
		break;
	}
	default:
		fprintf(stderr, "Unrecognized stat #%d\n", id);
		return -EINVAL;
	}
	return 0;
}

static int parse_stats_csv(const char *filename, struct stat_specs *specs,
			   struct verif_stats **statsp, int *stat_cntp)
{
	char line[4096];
	FILE *f;
	int err = 0;
	bool header = true;

	f = fopen(filename, "r");
	if (!f) {
		err = -errno;
		fprintf(stderr, "Failed to open '%s': %d\n", filename, err);
		return err;
	}

	*stat_cntp = 0;

	while (fgets(line, sizeof(line), f)) {
		char *input = line, *state = NULL, *next;
		struct verif_stats *st = NULL;
		int col = 0;

		if (!header) {
			void *tmp;

			tmp = realloc(*statsp, (*stat_cntp + 1) * sizeof(**statsp));
			if (!tmp) {
				err = -ENOMEM;
				goto cleanup;
			}
			*statsp = tmp;

			st = &(*statsp)[*stat_cntp];
			memset(st, 0, sizeof(*st));

			*stat_cntp += 1;
		}

		while ((next = strtok_r(state ? NULL : input, ",\n", &state))) {
			if (header) {
				/* for the first line, set up spec stats */
				err = parse_stat(next, specs);
				if (err)
					goto cleanup;
				continue;
			}

			/* for all other lines, parse values based on spec */
			if (col >= specs->spec_cnt) {
				fprintf(stderr, "Found extraneous column #%d in row #%d of '%s'\n",
					col, *stat_cntp, filename);
				err = -EINVAL;
				goto cleanup;
			}
			err = parse_stat_value(next, specs->ids[col], st);
			if (err)
				goto cleanup;
			col++;
		}

		if (header) {
			header = false;
			continue;
		}

		if (col < specs->spec_cnt) {
			fprintf(stderr, "Not enough columns in row #%d in '%s'\n",
				*stat_cntp, filename);
			err = -EINVAL;
			goto cleanup;
		}

		if (!st->file_name || !st->prog_name) {
			fprintf(stderr, "Row #%d in '%s' is missing file and/or program name\n",
				*stat_cntp, filename);
			err = -EINVAL;
			goto cleanup;
		}

		/* in comparison mode we can only check filters after we
		 * parsed entire line; if row should be ignored we pretend we
		 * never parsed it
		 */
		if (!should_process_prog(st->file_name, st->prog_name)) {
			free(st->file_name);
			free(st->prog_name);
			*stat_cntp -= 1;
		}
	}

	if (!feof(f)) {
		err = -errno;
		fprintf(stderr, "Failed I/O for '%s': %d\n", filename, err);
	}

cleanup:
	fclose(f);
	return err;
}

/* empty/zero stats for mismatched rows */
static const struct verif_stats fallback_stats = { .file_name = "", .prog_name = "" };

static bool is_key_stat(enum stat_id id)
{
	return id == FILE_NAME || id == PROG_NAME;
}

static void output_comp_header_underlines(void)
{
	int i, j, k;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		int id = env.output_spec.ids[i];
		int max_j = is_key_stat(id) ? 1 : 3;

		for (j = 0; j < max_j; j++) {
			int len = env.output_spec.lens[3 * i + j];

			printf("%s", i + j == 0 ? "" : COLUMN_SEP);

			for (k = 0; k < len; k++)
				printf("%c", HEADER_CHAR);
		}
	}
	printf("\n");
}

static void output_comp_headers(enum resfmt fmt)
{
	static const char *table_sfxs[3] = {" (A)", " (B)", " (DIFF)"};
	static const char *name_sfxs[3] = {"_base", "_comp", "_diff"};
	int i, j, len;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		int id = env.output_spec.ids[i];
		/* key stats don't have A/B/DIFF columns, they are common for both data sets */
		int max_j = is_key_stat(id) ? 1 : 3;

		for (j = 0; j < max_j; j++) {
			int *max_len = &env.output_spec.lens[3 * i + j];
			bool last = (i == env.output_spec.spec_cnt - 1) && (j == max_j - 1);
			const char *sfx;

			switch (fmt) {
			case RESFMT_TABLE_CALCLEN:
				sfx = is_key_stat(id) ? "" : table_sfxs[j];
				len = snprintf(NULL, 0, "%s%s", stat_defs[id].header, sfx);
				if (len > *max_len)
					*max_len = len;
				break;
			case RESFMT_TABLE:
				sfx = is_key_stat(id) ? "" : table_sfxs[j];
				printf("%s%-*s%s", i + j == 0 ? "" : COLUMN_SEP,
				       *max_len - (int)strlen(sfx), stat_defs[id].header, sfx);
				if (last)
					printf("\n");
				break;
			case RESFMT_CSV:
				sfx = is_key_stat(id) ? "" : name_sfxs[j];
				printf("%s%s%s", i + j == 0 ? "" : ",", stat_defs[id].names[0], sfx);
				if (last)
					printf("\n");
				break;
			}
		}
	}

	if (fmt == RESFMT_TABLE)
		output_comp_header_underlines();
}

static void output_comp_stats(const struct verif_stats *base, const struct verif_stats *comp,
			      enum resfmt fmt, bool last)
{
	char base_buf[1024] = {}, comp_buf[1024] = {}, diff_buf[1024] = {};
	int i;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		int id = env.output_spec.ids[i], len;
		int *max_len_base = &env.output_spec.lens[3 * i + 0];
		int *max_len_comp = &env.output_spec.lens[3 * i + 1];
		int *max_len_diff = &env.output_spec.lens[3 * i + 2];
		const char *base_str = NULL, *comp_str = NULL;
		long base_val = 0, comp_val = 0, diff_val = 0;

		prepare_value(base, id, &base_str, &base_val);
		prepare_value(comp, id, &comp_str, &comp_val);

		/* normalize all the outputs to be in string buffers for simplicity */
		if (is_key_stat(id)) {
			/* key stats (file and program name) are always strings */
			if (base != &fallback_stats)
				snprintf(base_buf, sizeof(base_buf), "%s", base_str);
			else
				snprintf(base_buf, sizeof(base_buf), "%s", comp_str);
		} else if (base_str) {
			snprintf(base_buf, sizeof(base_buf), "%s", base_str);
			snprintf(comp_buf, sizeof(comp_buf), "%s", comp_str);
			if (strcmp(base_str, comp_str) == 0)
				snprintf(diff_buf, sizeof(diff_buf), "%s", "MATCH");
			else
				snprintf(diff_buf, sizeof(diff_buf), "%s", "MISMATCH");
		} else {
			snprintf(base_buf, sizeof(base_buf), "%ld", base_val);
			snprintf(comp_buf, sizeof(comp_buf), "%ld", comp_val);

			diff_val = comp_val - base_val;
			if (base == &fallback_stats || comp == &fallback_stats || base_val == 0) {
				snprintf(diff_buf, sizeof(diff_buf), "%+ld (%+.2lf%%)",
					 diff_val, comp_val < base_val ? -100.0 : 100.0);
			} else {
				snprintf(diff_buf, sizeof(diff_buf), "%+ld (%+.2lf%%)",
					 diff_val, diff_val * 100.0 / base_val);
			}
		}

		switch (fmt) {
		case RESFMT_TABLE_CALCLEN:
			len = strlen(base_buf);
			if (len > *max_len_base)
				*max_len_base = len;
			if (!is_key_stat(id)) {
				len = strlen(comp_buf);
				if (len > *max_len_comp)
					*max_len_comp = len;
				len = strlen(diff_buf);
				if (len > *max_len_diff)
					*max_len_diff = len;
			}
			break;
		case RESFMT_TABLE: {
			/* string outputs are left-aligned, number outputs are right-aligned */
			const char *fmt = base_str ? "%s%-*s" : "%s%*s";

			printf(fmt, i == 0 ? "" : COLUMN_SEP, *max_len_base, base_buf);
			if (!is_key_stat(id)) {
				printf(fmt, COLUMN_SEP, *max_len_comp, comp_buf);
				printf(fmt, COLUMN_SEP, *max_len_diff, diff_buf);
			}
			if (i == env.output_spec.spec_cnt - 1)
				printf("\n");
			break;
		}
		case RESFMT_CSV:
			printf("%s%s", i == 0 ? "" : ",", base_buf);
			if (!is_key_stat(id)) {
				printf("%s%s", i == 0 ? "" : ",", comp_buf);
				printf("%s%s", i == 0 ? "" : ",", diff_buf);
			}
			if (i == env.output_spec.spec_cnt - 1)
				printf("\n");
			break;
		}
	}

	if (last && fmt == RESFMT_TABLE)
		output_comp_header_underlines();
}

static int cmp_stats_key(const struct verif_stats *base, const struct verif_stats *comp)
{
	int r;

	r = strcmp(base->file_name, comp->file_name);
	if (r != 0)
		return r;
	return strcmp(base->prog_name, comp->prog_name);
}

static int handle_comparison_mode(void)
{
	struct stat_specs base_specs = {}, comp_specs = {};
	enum resfmt cur_fmt;
	int err, i, j;

	if (env.filename_cnt != 2) {
		fprintf(stderr, "Comparison mode expects exactly two input CSV files!\n");
		argp_help(&argp, stderr, ARGP_HELP_USAGE, "veristat");
		return -EINVAL;
	}

	err = parse_stats_csv(env.filenames[0], &base_specs,
			      &env.baseline_stats, &env.baseline_stat_cnt);
	if (err) {
		fprintf(stderr, "Failed to parse stats from '%s': %d\n", env.filenames[0], err);
		return err;
	}
	err = parse_stats_csv(env.filenames[1], &comp_specs,
			      &env.prog_stats, &env.prog_stat_cnt);
	if (err) {
		fprintf(stderr, "Failed to parse stats from '%s': %d\n", env.filenames[1], err);
		return err;
	}

	/* To keep it simple we validate that the set and order of stats in
	 * both CSVs are exactly the same. This can be lifted with a bit more
	 * pre-processing later.
	 */
	if (base_specs.spec_cnt != comp_specs.spec_cnt) {
		fprintf(stderr, "Number of stats in '%s' and '%s' differs (%d != %d)!\n",
			env.filenames[0], env.filenames[1],
			base_specs.spec_cnt, comp_specs.spec_cnt);
		return -EINVAL;
	}
	for (i = 0; i < base_specs.spec_cnt; i++) {
		if (base_specs.ids[i] != comp_specs.ids[i]) {
			fprintf(stderr, "Stats composition differs between '%s' and '%s' (%s != %s)!\n",
				env.filenames[0], env.filenames[1],
				stat_defs[base_specs.ids[i]].names[0],
				stat_defs[comp_specs.ids[i]].names[0]);
			return -EINVAL;
		}
	}

	qsort(env.prog_stats, env.prog_stat_cnt, sizeof(*env.prog_stats), cmp_prog_stats);
	qsort(env.baseline_stats, env.baseline_stat_cnt, sizeof(*env.baseline_stats), cmp_prog_stats);

	/* for human-readable table output we need to do extra pass to
	 * calculate column widths, so we substitute current output format
	 * with RESFMT_TABLE_CALCLEN and later revert it back to RESFMT_TABLE
	 * and do everything again.
	 */
	if (env.out_fmt == RESFMT_TABLE)
		cur_fmt = RESFMT_TABLE_CALCLEN;
	else
		cur_fmt = env.out_fmt;

one_more_time:
	output_comp_headers(cur_fmt);

	/* If baseline and comparison datasets have different subset of rows
	 * (we match by 'object + prog' as a unique key) then assume
	 * empty/missing/zero value for rows that are missing in the opposite
	 * data set
	 */
	i = j = 0;
	while (i < env.baseline_stat_cnt || j < env.prog_stat_cnt) {
		bool last = (i == env.baseline_stat_cnt - 1) || (j == env.prog_stat_cnt - 1);
		const struct verif_stats *base, *comp;
		int r;

		base = i < env.baseline_stat_cnt ? &env.baseline_stats[i] : &fallback_stats;
		comp = j < env.prog_stat_cnt ? &env.prog_stats[j] : &fallback_stats;

		if (!base->file_name || !base->prog_name) {
			fprintf(stderr, "Entry #%d in '%s' doesn't have file and/or program name specified!\n",
				i, env.filenames[0]);
			return -EINVAL;
		}
		if (!comp->file_name || !comp->prog_name) {
			fprintf(stderr, "Entry #%d in '%s' doesn't have file and/or program name specified!\n",
				j, env.filenames[1]);
			return -EINVAL;
		}

		r = cmp_stats_key(base, comp);
		if (r == 0) {
			output_comp_stats(base, comp, cur_fmt, last);
			i++;
			j++;
		} else if (comp == &fallback_stats || r < 0) {
			output_comp_stats(base, &fallback_stats, cur_fmt, last);
			i++;
		} else {
			output_comp_stats(&fallback_stats, comp, cur_fmt, last);
			j++;
		}
	}

	if (cur_fmt == RESFMT_TABLE_CALCLEN) {
		cur_fmt = RESFMT_TABLE;
		goto one_more_time; /* ... this time with feeling */
	}

	return 0;
}

int main(int argc, char **argv)
{
	int err = 0, i;

	if (argp_parse(&argp, argc, argv, 0, NULL, NULL))
		return 1;

	if (env.verbose && env.quiet) {
		fprintf(stderr, "Verbose and quiet modes are incompatible, please specify just one or neither!\n");
		argp_help(&argp, stderr, ARGP_HELP_USAGE, "veristat");
		return 1;
	}
	if (env.verbose && env.log_level == 0)
		env.log_level = 1;

	if (env.output_spec.spec_cnt == 0)
		env.output_spec = default_output_spec;
	if (env.sort_spec.spec_cnt == 0)
		env.sort_spec = default_sort_spec;

	if (env.comparison_mode)
		err = handle_comparison_mode();
	else
		err = handle_verif_mode();

	free_verif_stats(env.prog_stats, env.prog_stat_cnt);
	free_verif_stats(env.baseline_stats, env.baseline_stat_cnt);
	for (i = 0; i < env.filename_cnt; i++)
		free(env.filenames[i]);
	free(env.filenames);
	for (i = 0; i < env.allow_filter_cnt; i++) {
		free(env.allow_filters[i].file_glob);
		free(env.allow_filters[i].prog_glob);
	}
	free(env.allow_filters);
	for (i = 0; i < env.deny_filter_cnt; i++) {
		free(env.deny_filters[i].file_glob);
		free(env.deny_filters[i].prog_glob);
	}
	free(env.deny_filters);
	return -err;
}
