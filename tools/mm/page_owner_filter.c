// SPDX-License-Identifier: GPL-2.0
/*
 * User-space helper to filter page_owner output per-fd
 *
 * Example use:
 *   ./page_owner_filter -m handle
 *   ./page_owner_filter -m stack_handle
 *   ./page_owner_filter -n 0,1,2
 *
 * See Documentation/mm/page_owner.rst
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>

#define MAX_CMD_LEN	512

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n", prog);
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -m, --mode MODE      : print_mode (stack, handle, or stack_handle)\n");
	fprintf(stderr, "  -n, --nid NID_LIST   : NUMA node IDs (comma-separated or ranges)\n");
	fprintf(stderr, "  -o, --output FILE    : output file (default: stdout)\n");
	fprintf(stderr, "  -h, --help           : show this help message\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  %s -m stack\n", prog);
	fprintf(stderr, "  %s -m handle\n", prog);
	fprintf(stderr, "  %s -m stack_handle\n", prog);
	fprintf(stderr, "  %s -m stack -o output.txt\n", prog);
	fprintf(stderr, "  %s -n 0,1,2\n", prog);
	fprintf(stderr, "  %s -m stack -n 0\n", prog);
}

static int validate_mode(const char *mode)
{
	if (strcmp(mode, "stack") == 0 ||
	    strcmp(mode, "handle") == 0 ||
	    strcmp(mode, "stack_handle") == 0)
		return 0;

	fprintf(stderr, "Error: Invalid mode '%s'\n", mode);
	fprintf(stderr, "Valid modes: stack, handle, stack_handle\n");
	return -1;
}

static int validate_nid_list(const char *nid_list)
{
	const char *p;
	int i = 0;
	int has_digit = 0;
	int in_range = 0;
	int prev_num = 0;
	int curr_num = 0;

	if (!nid_list || strlen(nid_list) == 0)
		return 0;

	for (p = nid_list; *p; p++) {
		if (*p == ',') {
			if (!has_digit) {
				fprintf(stderr, "Error: Invalid nid_list format\n");
				return -1;
			}
			if (in_range && prev_num > curr_num) {
				fprintf(stderr,
					"Error: Invalid range %d-%d (start must be <= end)\n",
					prev_num, curr_num);
				return -1;
			}
			i = 0;
			has_digit = 0;
			in_range = 0;
			prev_num = 0;
			curr_num = 0;
			continue;
		}

		if (*p == '-') {
			if (!has_digit) {
				fprintf(stderr,
					"Error: Invalid nid_list format ");
				fprintf(stderr,
					"(dash without preceding number)\n");
				return -1;
			}
			if (in_range) {
				fprintf(stderr, "Error: Multiple dashes in nid_list\n");
				return -1;
			}
			prev_num = curr_num;
			curr_num = 0;
			i = 0;
			has_digit = 0;
			in_range = 1;
			continue;
		}

		if (!isdigit((unsigned char)*p)) {
			fprintf(stderr, "Error: Invalid character '%c' in nid_list\n", *p);
			return -1;
		}

		if (i > 5) {
			fprintf(stderr, "Error: NID too long (max 65536)\n");
			return -1;
		}
		curr_num = curr_num * 10 + (*p - '0');
		i++;
		has_digit = 1;
	}

	if (!has_digit) {
		fprintf(stderr, "Error: Invalid nid_list format\n");
		return -1;
	}

	if (in_range && prev_num > curr_num) {
		fprintf(stderr,
			"Error: Invalid range %d-%d (start must be <= end)\n",
			prev_num, curr_num);
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	const char *output_file = NULL;
	char filter_cmd[MAX_CMD_LEN];
	FILE *output = NULL;
	int fd = -1;
	ssize_t ret;
	char buf[4096];
	int opt;
	size_t cmd_len = 0;

	signal(SIGPIPE, SIG_IGN);

	static struct option long_options[] = {
		{"mode",	required_argument, 0, 'm'},
		{"nid",		required_argument, 0, 'n'},
		{"output",	required_argument, 0, 'o'},
		{"help",	no_argument,	   0, 'h'},
		{0, 0, 0, 0}
	};

	filter_cmd[0] = '\0';

	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
				usage(argv[0]);
				return 0;
			}
		}
	}

	/* Check if page_owner exists and is readable */
	if (access("/sys/kernel/debug/page_owner", F_OK) != 0) {
		if (errno == ENOENT)
			fprintf(stderr, "Error: /sys/kernel/debug/page_owner does not exist\n");
		else
			perror("Error accessing /sys/kernel/debug/page_owner");
		fprintf(stderr, "Make sure page_owner is enabled in kernel\n");
		return 1;
	}

	while ((opt = getopt_long(argc, argv, "m:n:o:h", long_options, NULL)) != -1) {
		int len;

		switch (opt) {
		case 'm': {
			const char *mode = optarg;

			if (validate_mode(mode) < 0)
				return 1;
			len = snprintf(filter_cmd + cmd_len, MAX_CMD_LEN - cmd_len,
				       "%smode=%s", cmd_len > 0 ? " " : "", mode);
			if (len < 0 || cmd_len + len >= MAX_CMD_LEN) {
				fprintf(stderr, "Error: Command too long\n");
				return 1;
			}
			cmd_len += len;
			break;
		}
		case 'n': {
			const char *nid_list = optarg;

			if (validate_nid_list(nid_list) < 0)
				return 1;
			len = snprintf(filter_cmd + cmd_len, MAX_CMD_LEN - cmd_len,
				       "%snid=%s", cmd_len > 0 ? " " : "", nid_list);
			if (len < 0 || cmd_len + len >= MAX_CMD_LEN) {
				fprintf(stderr, "Error: Command too long\n");
				return 1;
			}
			cmd_len += len;
			break;
		}
		case 'o':
			output_file = optarg;
			break;
		case 'h':
			/* Already handled above */
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	/* At least one filter must be specified */
	if (cmd_len == 0) {
		fprintf(stderr, "Error: At least one filter (-m or -n) must be specified\n\n");
		usage(argv[0]);
		return 1;
	}

	/* Open page_owner for read-write - this will fail if kernel doesn't support write */
	fd = open("/sys/kernel/debug/page_owner", O_RDWR);
	if (fd < 0) {
		if (errno == EACCES || errno == EPERM) {
			fprintf(stderr, "Error: /sys/kernel/debug/page_owner ");
			fprintf(stderr, "does not support write access\n");
			fprintf(stderr, "This kernel does not support ");
			fprintf(stderr, "per-fd filtering.\n");
			fprintf(stderr, "Please ensure you have a kernel with ");
			fprintf(stderr, "per-fd filtering support.\n");
		} else {
			perror("Error opening /sys/kernel/debug/page_owner");
		}
		return 1;
	}

	if (output_file) {
		output = fopen(output_file, "w");
		if (!output) {
			perror("open output file");
			close(fd);
			return 1;
		}
	} else {
		output = stdout;
	}

	ret = write(fd, filter_cmd, strlen(filter_cmd));

	if (ret < 0) {
		if (errno == EINVAL) {
			fprintf(stderr, "Error: Kernel rejected the filter command.\n");
			fprintf(stderr, "Possible causes:\n");
			fprintf(stderr, "  - Kernel does not support per-fd filtering\n");
			fprintf(stderr, "  - NUMA node has no memory\n");
			fprintf(stderr, "  - Unknown reason\n");
		} else {
			perror("write filter command");
		}
		goto out;
	}

	if ((size_t)ret != strlen(filter_cmd))
		fprintf(stderr, "Warning: Partial write (%zd/%zu)\n", ret, strlen(filter_cmd));

	/* Read and display filtered output */
	ret = 0;
	while ((ret = read(fd, buf, sizeof(buf))) > 0) {
		size_t written = fwrite(buf, 1, ret, output);

		if (written != (size_t)ret) {
			if (errno == EPIPE) {
				/* Pipe closed, treat as success */
				ret = 0;
				goto out;
			}
			perror("write output");
			ret = -1;
			goto out;
		}
	}

	if (ret < 0) {
		perror("read page_owner");
		goto out;
	}

	if (fflush(output)) {
		if (errno == EPIPE) {
			/* Pipe closed, treat as success */
			ret = 0;
		} else {
			perror("flush output");
			ret = -1;
		}
	}

out:
	close(fd);
	if (output != stdout)
		fclose(output);
	return ret < 0 ? 1 : 0;
}
