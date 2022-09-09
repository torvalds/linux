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
	int lens[ALL_STATS_CNT];
};

static struct env {
	char **filenames;
	int filename_cnt;
	bool verbose;

	struct verif_stats *prog_stats;
	int prog_stat_cnt;

	struct stat_specs output_spec;
	struct stat_specs sort_spec;
} env;

static int libbpf_print_fn(enum libbpf_print_level level,
		    const char *format, va_list args)
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
"veristat    BPF verifier stats collection tool.\n"
"\n"
"USAGE: veristat <obj-file> [<obj-file>...]\n";

static const struct argp_option opts[] = {
	{ NULL, 'h', NULL, OPTION_HIDDEN, "Show the full help" },
	{ "verbose", 'v', NULL, 0, "Verbose mode" },
	{ "output", 'o', "SPEC", 0, "Specify output stats" },
	{ "sort", 's', "SPEC", 0, "Specify sort order" },
	{},
};

static int parse_stats(const char *stats_str, struct stat_specs *specs);

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
	case 'o':
		err = parse_stats(arg, &env.output_spec);
		if (err)
			return err;
		break;
	case 's':
		err = parse_stats(arg, &env.sort_spec);
		if (err)
			return err;
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
	[DURATION] = { "Duration, us", {"duration", "dur"}, },
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

static char verif_log_buf[64 * 1024];

static int parse_verif_log(const char *buf, size_t buf_sz, struct verif_stats *s)
{
	const char *next;
	int pos;

	for (pos = 0; buf[0]; buf = next) {
		if (buf[0] == '\n')
			buf++;
		next = strchrnul(&buf[pos], '\n');

		if (1 == sscanf(buf, "verification time %ld usec\n", &s->stats[DURATION]))
			continue;
		if (6 == sscanf(buf, "processed %ld insns (limit %*d) max_states_per_insn %ld total_states %ld peak_states %ld mark_read %ld",
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
		bpf_program__set_log_level(prog, 1 | 4); /* stats + log */
	} else {
		bpf_program__set_log_buf(prog, buf, buf_sz);
		bpf_program__set_log_level(prog, 4); /* only verifier stats */
	}
	verif_log_buf[0] = '\0';

	err = bpf_object__load(obj);

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

	old_libbpf_print_fn = libbpf_set_print(libbpf_print_fn);

	obj = bpf_object__open_file(filename, &opts);
	if (!obj) {
		err = -errno;
		fprintf(stderr, "Failed to open '%s': %d\n", filename, err);
		goto cleanup;
	}

	bpf_object__for_each_program(prog, obj) {
		prog_cnt++;
	}

	if (prog_cnt == 1) {
		prog = bpf_object__next_program(obj, NULL);
		bpf_program__set_autoload(prog, true);
		process_prog(filename, obj, prog);
		bpf_object__close(obj);
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

static void output_headers(bool calc_len)
{
	int i, len;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		int id = env.output_spec.ids[i];
		int *max_len = &env.output_spec.lens[i];

		if (calc_len) {
			len = snprintf(NULL, 0, "%s", stat_defs[id].header);
			if (len > *max_len)
				*max_len = len;
		} else {
			printf("%s%-*s", i == 0 ? "" : COLUMN_SEP,  *max_len, stat_defs[id].header);
		}
	}

	if (!calc_len)
		printf("\n");
}

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

static void output_stats(const struct verif_stats *s, bool calc_len)
{
	int i;

	for (i = 0; i < env.output_spec.spec_cnt; i++) {
		int id = env.output_spec.ids[i];
		int *max_len = &env.output_spec.lens[i], len;
		const char *str = NULL;
		long val = 0;

		switch (id) {
		case FILE_NAME:
			str = s->file_name;
			break;
		case PROG_NAME:
			str = s->prog_name;
			break;
		case VERDICT:
			str = s->stats[VERDICT] ? "success" : "failure";
			break;
		case DURATION:
		case TOTAL_INSNS:
		case TOTAL_STATES:
		case PEAK_STATES:
		case MAX_STATES_PER_INSN:
		case MARK_READ_MAX_LEN:
			val = s->stats[id];
			break;
		default:
			fprintf(stderr, "Unrecognized stat #%d\n", id);
			exit(1);
		}

		if (calc_len) {
			if (str)
				len = snprintf(NULL, 0, "%s", str);
			else
				len = snprintf(NULL, 0, "%ld", val);
			if (len > *max_len)
				*max_len = len;
		} else {
			if (str)
				printf("%s%-*s", i == 0 ? "" : COLUMN_SEP, *max_len, str);
			else
				printf("%s%*ld", i == 0 ? "" : COLUMN_SEP,  *max_len, val);
		}
	}

	if (!calc_len)
		printf("\n");
}

int main(int argc, char **argv)
{
	static const struct argp argp = {
		.options = opts,
		.parser = parse_arg,
		.doc = argp_program_doc,
	};
	int err = 0, i;

	if (argp_parse(&argp, argc, argv, 0, NULL, NULL))
		return 1;

	if (env.filename_cnt == 0) {
		fprintf(stderr, "Please provide path to BPF object file!\n");
		argp_help(&argp, stderr, ARGP_HELP_USAGE, "veristat");
		return 1;
	}

	if (env.output_spec.spec_cnt == 0)
		env.output_spec = default_output_spec;
	if (env.sort_spec.spec_cnt == 0)
		env.sort_spec = default_sort_spec;

	for (i = 0; i < env.filename_cnt; i++) {
		err = process_obj(env.filenames[i]);
		if (err) {
			fprintf(stderr, "Failed to process '%s': %d\n", env.filenames[i], err);
			goto cleanup;
		}
	}

	qsort(env.prog_stats, env.prog_stat_cnt, sizeof(*env.prog_stats), cmp_prog_stats);

	/* calculate column widths */
	output_headers(true);
	for (i = 0; i < env.prog_stat_cnt; i++) {
		output_stats(&env.prog_stats[i], true);
	}

	/* actually output the table */
	output_headers(false);
	output_header_underlines();
	for (i = 0; i < env.prog_stat_cnt; i++) {
		output_stats(&env.prog_stats[i], false);
	}
	output_header_underlines();
	printf("\n");

	printf("Done. Processed %d object files, %d programs.\n",
	       env.filename_cnt, env.prog_stat_cnt);

cleanup:
	for (i = 0; i < env.prog_stat_cnt; i++) {
		free(env.prog_stats[i].file_name);
		free(env.prog_stats[i].prog_name);
	}
	free(env.prog_stats);
	for (i = 0; i < env.filename_cnt; i++)
		free(env.filenames[i]);
	free(env.filenames);
	return -err;
}
