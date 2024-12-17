// SPDX-License-Identifier: GPL-2.0
/*
 * User-space helper to sort the output of /sys/kernel/debug/page_owner
 *
 * Example use:
 * cat /sys/kernel/debug/page_owner > page_owner_full.txt
 * ./page_owner_sort page_owner_full.txt sorted_page_owner.txt
 * Or sort by total memory:
 * ./page_owner_sort -m page_owner_full.txt sorted_page_owner.txt
 *
 * See Documentation/mm/page_owner.rst
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <errno.h>
#include <linux/types.h>
#include <getopt.h>

#define bool int
#define true 1
#define false 0
#define TASK_COMM_LEN 16

struct block_list {
	char *txt;
	char *comm; // task command name
	char *stacktrace;
	__u64 ts_nsec;
	int len;
	int num;
	int page_num;
	pid_t pid;
	pid_t tgid;
	int allocator;
};
enum FILTER_BIT {
	FILTER_PID = 1<<1,
	FILTER_TGID = 1<<2,
	FILTER_COMM = 1<<3
};
enum CULL_BIT {
	CULL_PID = 1<<1,
	CULL_TGID = 1<<2,
	CULL_COMM = 1<<3,
	CULL_STACKTRACE = 1<<4,
	CULL_ALLOCATOR = 1<<5
};
enum ALLOCATOR_BIT {
	ALLOCATOR_CMA = 1<<1,
	ALLOCATOR_SLAB = 1<<2,
	ALLOCATOR_VMALLOC = 1<<3,
	ALLOCATOR_OTHERS = 1<<4
};
enum ARG_TYPE {
	ARG_TXT, ARG_COMM, ARG_STACKTRACE, ARG_ALLOC_TS, ARG_CULL_TIME,
	ARG_PAGE_NUM, ARG_PID, ARG_TGID, ARG_UNKNOWN, ARG_ALLOCATOR
};
enum SORT_ORDER {
	SORT_ASC = 1,
	SORT_DESC = -1,
};
enum COMP_FLAG {
	COMP_NO_FLAG = 0,
	COMP_ALLOC = 1<<0,
	COMP_PAGE_NUM = 1<<1,
	COMP_PID = 1<<2,
	COMP_STACK = 1<<3,
	COMP_NUM = 1<<4,
	COMP_TGID = 1<<5,
	COMP_COMM = 1<<6
};
struct filter_condition {
	pid_t *pids;
	pid_t *tgids;
	char **comms;
	int pids_size;
	int tgids_size;
	int comms_size;
};
struct sort_condition {
	int (**cmps)(const void *, const void *);
	int *signs;
	int size;
};
static struct filter_condition fc;
static struct sort_condition sc;
static regex_t order_pattern;
static regex_t pid_pattern;
static regex_t tgid_pattern;
static regex_t comm_pattern;
static regex_t ts_nsec_pattern;
static struct block_list *list;
static int list_size;
static int max_size;
static int cull;
static int filter;
static bool debug_on;

static void set_single_cmp(int (*cmp)(const void *, const void *), int sign);

int read_block(char *buf, char *ext_buf, int buf_size, FILE *fin)
{
	char *curr = buf, *const buf_end = buf + buf_size;

	while (buf_end - curr > 1 && fgets(curr, buf_end - curr, fin)) {
		if (*curr == '\n') { /* empty line */
			return curr - buf;
		}
		if (!strncmp(curr, "PFN", 3)) {
			strcpy(ext_buf, curr);
			continue;
		}
		curr += strlen(curr);
	}

	return -1; /* EOF or no space left in buf. */
}

static int compare_txt(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return strcmp(l1->txt, l2->txt);
}

static int compare_stacktrace(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return strcmp(l1->stacktrace, l2->stacktrace);
}

static int compare_num(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l1->num - l2->num;
}

static int compare_page_num(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l1->page_num - l2->page_num;
}

static int compare_pid(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l1->pid - l2->pid;
}

static int compare_tgid(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l1->tgid - l2->tgid;
}

static int compare_allocator(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l1->allocator - l2->allocator;
}

static int compare_comm(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return strcmp(l1->comm, l2->comm);
}

static int compare_ts(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l1->ts_nsec < l2->ts_nsec ? -1 : 1;
}

static int compare_cull_condition(const void *p1, const void *p2)
{
	if (cull == 0)
		return compare_txt(p1, p2);
	if ((cull & CULL_STACKTRACE) && compare_stacktrace(p1, p2))
		return compare_stacktrace(p1, p2);
	if ((cull & CULL_PID) && compare_pid(p1, p2))
		return compare_pid(p1, p2);
	if ((cull & CULL_TGID) && compare_tgid(p1, p2))
		return compare_tgid(p1, p2);
	if ((cull & CULL_COMM) && compare_comm(p1, p2))
		return compare_comm(p1, p2);
	if ((cull & CULL_ALLOCATOR) && compare_allocator(p1, p2))
		return compare_allocator(p1, p2);
	return 0;
}

static int compare_sort_condition(const void *p1, const void *p2)
{
	int cmp = 0;

	for (int i = 0; i < sc.size; ++i)
		if (cmp == 0)
			cmp = sc.signs[i] * sc.cmps[i](p1, p2);
	return cmp;
}

static int remove_pattern(regex_t *pattern, char *buf, int len)
{
	regmatch_t pmatch[2];
	int err;

	err = regexec(pattern, buf, 2, pmatch, REG_NOTBOL);
	if (err != 0 || pmatch[1].rm_so == -1)
		return len;

	memcpy(buf + pmatch[1].rm_so,
		buf + pmatch[1].rm_eo, len - pmatch[1].rm_eo);

	return len - (pmatch[1].rm_eo - pmatch[1].rm_so);
}

static int search_pattern(regex_t *pattern, char *pattern_str, char *buf)
{
	int err, val_len;
	regmatch_t pmatch[2];

	err = regexec(pattern, buf, 2, pmatch, REG_NOTBOL);
	if (err != 0 || pmatch[1].rm_so == -1) {
		if (debug_on)
			fprintf(stderr, "no matching pattern in %s\n", buf);
		return -1;
	}
	val_len = pmatch[1].rm_eo - pmatch[1].rm_so;

	memcpy(pattern_str, buf + pmatch[1].rm_so, val_len);

	return 0;
}

static bool check_regcomp(regex_t *pattern, const char *regex)
{
	int err;

	err = regcomp(pattern, regex, REG_EXTENDED | REG_NEWLINE);
	if (err != 0 || pattern->re_nsub != 1) {
		fprintf(stderr, "Invalid pattern %s code %d\n", regex, err);
		return false;
	}
	return true;
}

static char **explode(char sep, const char *str, int *size)
{
	int count = 0, len = strlen(str);
	int lastindex = -1, j = 0;

	for (int i = 0; i < len; i++)
		if (str[i] == sep)
			count++;
	char **ret = calloc(++count, sizeof(char *));

	for (int i = 0; i < len; i++) {
		if (str[i] == sep) {
			ret[j] = calloc(i - lastindex, sizeof(char));
			memcpy(ret[j++], str + lastindex + 1, i - lastindex - 1);
			lastindex = i;
		}
	}
	if (lastindex <= len - 1) {
		ret[j] = calloc(len - lastindex, sizeof(char));
		memcpy(ret[j++], str + lastindex + 1, strlen(str) - 1 - lastindex);
	}
	*size = j;
	return ret;
}

static void free_explode(char **arr, int size)
{
	for (int i = 0; i < size; i++)
		free(arr[i]);
	free(arr);
}

# define FIELD_BUFF 25

static int get_page_num(char *buf)
{
	int order_val;
	char order_str[FIELD_BUFF] = {0};
	char *endptr;

	search_pattern(&order_pattern, order_str, buf);
	errno = 0;
	order_val = strtol(order_str, &endptr, 10);
	if (order_val > 64 || errno != 0 || endptr == order_str || *endptr != '\0') {
		if (debug_on)
			fprintf(stderr, "wrong order in follow buf:\n%s\n", buf);
		return 0;
	}

	return 1 << order_val;
}

static pid_t get_pid(char *buf)
{
	pid_t pid;
	char pid_str[FIELD_BUFF] = {0};
	char *endptr;

	search_pattern(&pid_pattern, pid_str, buf);
	errno = 0;
	pid = strtol(pid_str, &endptr, 10);
	if (errno != 0 || endptr == pid_str || *endptr != '\0') {
		if (debug_on)
			fprintf(stderr, "wrong/invalid pid in follow buf:\n%s\n", buf);
		return -1;
	}

	return pid;

}

static pid_t get_tgid(char *buf)
{
	pid_t tgid;
	char tgid_str[FIELD_BUFF] = {0};
	char *endptr;

	search_pattern(&tgid_pattern, tgid_str, buf);
	errno = 0;
	tgid = strtol(tgid_str, &endptr, 10);
	if (errno != 0 || endptr == tgid_str || *endptr != '\0') {
		if (debug_on)
			fprintf(stderr, "wrong/invalid tgid in follow buf:\n%s\n", buf);
		return -1;
	}

	return tgid;

}

static __u64 get_ts_nsec(char *buf)
{
	__u64 ts_nsec;
	char ts_nsec_str[FIELD_BUFF] = {0};
	char *endptr;

	search_pattern(&ts_nsec_pattern, ts_nsec_str, buf);
	errno = 0;
	ts_nsec = strtoull(ts_nsec_str, &endptr, 10);
	if (errno != 0 || endptr == ts_nsec_str || *endptr != '\0') {
		if (debug_on)
			fprintf(stderr, "wrong ts_nsec in follow buf:\n%s\n", buf);
		return -1;
	}

	return ts_nsec;
}

static char *get_comm(char *buf)
{
	char *comm_str = malloc(TASK_COMM_LEN);

	memset(comm_str, 0, TASK_COMM_LEN);

	search_pattern(&comm_pattern, comm_str, buf);
	errno = 0;
	if (errno != 0) {
		if (debug_on)
			fprintf(stderr, "wrong comm in follow buf:\n%s\n", buf);
		free(comm_str);
		return NULL;
	}

	return comm_str;
}

static int get_arg_type(const char *arg)
{
	if (!strcmp(arg, "pid") || !strcmp(arg, "p"))
		return ARG_PID;
	else if (!strcmp(arg, "tgid") || !strcmp(arg, "tg"))
		return ARG_TGID;
	else if (!strcmp(arg, "name") || !strcmp(arg, "n"))
		return  ARG_COMM;
	else if (!strcmp(arg, "stacktrace") || !strcmp(arg, "st"))
		return ARG_STACKTRACE;
	else if (!strcmp(arg, "txt") || !strcmp(arg, "T"))
		return ARG_TXT;
	else if (!strcmp(arg, "alloc_ts") || !strcmp(arg, "at"))
		return ARG_ALLOC_TS;
	else if (!strcmp(arg, "allocator") || !strcmp(arg, "ator"))
		return ARG_ALLOCATOR;
	else {
		return ARG_UNKNOWN;
	}
}

static int get_allocator(const char *buf, const char *migrate_info)
{
	char *tmp, *first_line, *second_line;
	int allocator = 0;

	if (strstr(migrate_info, "CMA"))
		allocator |= ALLOCATOR_CMA;
	if (strstr(migrate_info, "slab"))
		allocator |= ALLOCATOR_SLAB;
	tmp = strstr(buf, "__vmalloc_node_range");
	if (tmp) {
		second_line = tmp;
		while (*tmp != '\n')
			tmp--;
		tmp--;
		while (*tmp != '\n')
			tmp--;
		first_line = ++tmp;
		tmp = strstr(tmp, "alloc_pages");
		if (tmp && first_line <= tmp && tmp < second_line)
			allocator |= ALLOCATOR_VMALLOC;
	}
	if (allocator == 0)
		allocator = ALLOCATOR_OTHERS;
	return allocator;
}

static bool match_num_list(int num, int *list, int list_size)
{
	for (int i = 0; i < list_size; ++i)
		if (list[i] == num)
			return true;
	return false;
}

static bool match_str_list(const char *str, char **list, int list_size)
{
	for (int i = 0; i < list_size; ++i)
		if (!strcmp(list[i], str))
			return true;
	return false;
}

static bool is_need(char *buf)
{
	if ((filter & FILTER_PID) && !match_num_list(get_pid(buf), fc.pids, fc.pids_size))
		return false;
	if ((filter & FILTER_TGID) &&
		!match_num_list(get_tgid(buf), fc.tgids, fc.tgids_size))
		return false;

	char *comm = get_comm(buf);

	if ((filter & FILTER_COMM) &&
	!match_str_list(comm, fc.comms, fc.comms_size)) {
		free(comm);
		return false;
	}
	free(comm);
	return true;
}

static bool add_list(char *buf, int len, char *ext_buf)
{
	if (list_size == max_size) {
		fprintf(stderr, "max_size too small??\n");
		return false;
	}
	if (!is_need(buf))
		return true;
	list[list_size].pid = get_pid(buf);
	list[list_size].tgid = get_tgid(buf);
	list[list_size].comm = get_comm(buf);
	list[list_size].txt = malloc(len+1);
	if (!list[list_size].txt) {
		fprintf(stderr, "Out of memory\n");
		return false;
	}
	memcpy(list[list_size].txt, buf, len);
	if (sc.cmps[0] != compare_ts) {
		len = remove_pattern(&ts_nsec_pattern, list[list_size].txt, len);
	}
	list[list_size].txt[len] = 0;
	list[list_size].len = len;
	list[list_size].num = 1;
	list[list_size].page_num = get_page_num(buf);

	list[list_size].stacktrace = strchr(list[list_size].txt, '\n') ?: "";
	if (*list[list_size].stacktrace == '\n')
		list[list_size].stacktrace++;
	list[list_size].ts_nsec = get_ts_nsec(buf);
	list[list_size].allocator = get_allocator(buf, ext_buf);
	list_size++;
	if (list_size % 1000 == 0) {
		printf("loaded %d\r", list_size);
		fflush(stdout);
	}
	return true;
}

static bool parse_cull_args(const char *arg_str)
{
	int size = 0;
	char **args = explode(',', arg_str, &size);

	for (int i = 0; i < size; ++i) {
		int arg_type = get_arg_type(args[i]);

		if (arg_type == ARG_PID)
			cull |= CULL_PID;
		else if (arg_type == ARG_TGID)
			cull |= CULL_TGID;
		else if (arg_type == ARG_COMM)
			cull |= CULL_COMM;
		else if (arg_type == ARG_STACKTRACE)
			cull |= CULL_STACKTRACE;
		else if (arg_type == ARG_ALLOCATOR)
			cull |= CULL_ALLOCATOR;
		else {
			free_explode(args, size);
			return false;
		}
	}
	free_explode(args, size);
	if (sc.size == 0)
		set_single_cmp(compare_num, SORT_DESC);
	return true;
}

static void set_single_cmp(int (*cmp)(const void *, const void *), int sign)
{
	if (sc.signs == NULL || sc.size < 1)
		sc.signs = calloc(1, sizeof(int));
	sc.signs[0] = sign;
	if (sc.cmps == NULL || sc.size < 1)
		sc.cmps = calloc(1, sizeof(int *));
	sc.cmps[0] = cmp;
	sc.size = 1;
}

static bool parse_sort_args(const char *arg_str)
{
	int size = 0;

	if (sc.size != 0) { /* reset sort_condition */
		free(sc.signs);
		free(sc.cmps);
		size = 0;
	}

	char **args = explode(',', arg_str, &size);

	sc.signs = calloc(size, sizeof(int));
	sc.cmps = calloc(size, sizeof(int *));
	for (int i = 0; i < size; ++i) {
		int offset = 0;

		sc.signs[i] = SORT_ASC;
		if (args[i][0] == '-' || args[i][0] == '+') {
			if (args[i][0] == '-')
				sc.signs[i] = SORT_DESC;
			offset = 1;
		}

		int arg_type = get_arg_type(args[i]+offset);

		if (arg_type == ARG_PID)
			sc.cmps[i] = compare_pid;
		else if (arg_type == ARG_TGID)
			sc.cmps[i] = compare_tgid;
		else if (arg_type == ARG_COMM)
			sc.cmps[i] = compare_comm;
		else if (arg_type == ARG_STACKTRACE)
			sc.cmps[i] = compare_stacktrace;
		else if (arg_type == ARG_ALLOC_TS)
			sc.cmps[i] = compare_ts;
		else if (arg_type == ARG_TXT)
			sc.cmps[i] = compare_txt;
		else if (arg_type == ARG_ALLOCATOR)
			sc.cmps[i] = compare_allocator;
		else {
			free_explode(args, size);
			sc.size = 0;
			return false;
		}
	}
	sc.size = size;
	free_explode(args, size);
	return true;
}

static int *parse_nums_list(char *arg_str, int *list_size)
{
	int size = 0;
	char **args = explode(',', arg_str, &size);
	int *list = calloc(size, sizeof(int));

	errno = 0;
	for (int i = 0; i < size; ++i) {
		char *endptr = NULL;

		list[i] = strtol(args[i], &endptr, 10);
		if (errno != 0 || endptr == args[i] || *endptr != '\0') {
			free(list);
			return NULL;
		}
	}
	*list_size = size;
	free_explode(args, size);
	return list;
}

static void print_allocator(FILE *out, int allocator)
{
	fprintf(out, "allocated by ");
	if (allocator & ALLOCATOR_CMA)
		fprintf(out, "CMA ");
	if (allocator & ALLOCATOR_SLAB)
		fprintf(out, "SLAB ");
	if (allocator & ALLOCATOR_VMALLOC)
		fprintf(out, "VMALLOC ");
	if (allocator & ALLOCATOR_OTHERS)
		fprintf(out, "OTHERS ");
}

#define BUF_SIZE	(128 * 1024)

static void usage(void)
{
	printf("Usage: ./page_owner_sort [OPTIONS] <input> <output>\n"
		"-a\t\t\tSort by memory allocation time.\n"
		"-m\t\t\tSort by total memory.\n"
		"-n\t\t\tSort by task command name.\n"
		"-p\t\t\tSort by pid.\n"
		"-P\t\t\tSort by tgid.\n"
		"-s\t\t\tSort by the stacktrace.\n"
		"-t\t\t\tSort by number of times record is seen (default).\n\n"
		"--pid <pidlist>\t\tSelect by pid. This selects the information"
		" of\n\t\t\tblocks whose process ID numbers appear in <pidlist>.\n"
		"--tgid <tgidlist>\tSelect by tgid. This selects the information"
		" of\n\t\t\tblocks whose Thread Group ID numbers appear in "
		"<tgidlist>.\n"
		"--name <cmdlist>\tSelect by command name. This selects the"
		" information\n\t\t\tof blocks whose command name appears in"
		" <cmdlist>.\n"
		"--cull <rules>\t\tCull by user-defined rules. <rules> is a "
		"single\n\t\t\targument in the form of a comma-separated list "
		"with some\n\t\t\tcommon fields predefined (pid, tgid, comm, "
		"stacktrace, allocator)\n"
		"--sort <order>\t\tSpecify sort order as: [+|-]key[,[+|-]key[,...]]\n"
	);
}

int main(int argc, char **argv)
{
	FILE *fin, *fout;
	char *buf, *ext_buf;
	int i, count, compare_flag;
	struct stat st;
	int opt;
	struct option longopts[] = {
		{ "pid", required_argument, NULL, 1 },
		{ "tgid", required_argument, NULL, 2 },
		{ "name", required_argument, NULL, 3 },
		{ "cull",  required_argument, NULL, 4 },
		{ "sort",  required_argument, NULL, 5 },
		{ 0, 0, 0, 0},
	};

	compare_flag = COMP_NO_FLAG;

	while ((opt = getopt_long(argc, argv, "admnpstP", longopts, NULL)) != -1)
		switch (opt) {
		case 'a':
			compare_flag |= COMP_ALLOC;
			break;
		case 'd':
			debug_on = true;
			break;
		case 'm':
			compare_flag |= COMP_PAGE_NUM;
			break;
		case 'p':
			compare_flag |= COMP_PID;
			break;
		case 's':
			compare_flag |= COMP_STACK;
			break;
		case 't':
			compare_flag |= COMP_NUM;
			break;
		case 'P':
			compare_flag |= COMP_TGID;
			break;
		case 'n':
			compare_flag |= COMP_COMM;
			break;
		case 1:
			filter = filter | FILTER_PID;
			fc.pids = parse_nums_list(optarg, &fc.pids_size);
			if (fc.pids == NULL) {
				fprintf(stderr, "wrong/invalid pid in from the command line:%s\n",
						optarg);
				exit(1);
			}
			break;
		case 2:
			filter = filter | FILTER_TGID;
			fc.tgids = parse_nums_list(optarg, &fc.tgids_size);
			if (fc.tgids == NULL) {
				fprintf(stderr, "wrong/invalid tgid in from the command line:%s\n",
						optarg);
				exit(1);
			}
			break;
		case 3:
			filter = filter | FILTER_COMM;
			fc.comms = explode(',', optarg, &fc.comms_size);
			break;
		case 4:
			if (!parse_cull_args(optarg)) {
				fprintf(stderr, "wrong argument after --cull option:%s\n",
						optarg);
				exit(1);
			}
			break;
		case 5:
			if (!parse_sort_args(optarg)) {
				fprintf(stderr, "wrong argument after --sort option:%s\n",
						optarg);
				exit(1);
			}
			break;
		default:
			usage();
			exit(1);
		}

	if (optind >= (argc - 1)) {
		usage();
		exit(1);
	}

	/* Only one compare option is allowed, yet we also want handle the
	 * default case were no option is provided, but we still want to
	 * match the behavior of the -t option (compare by number of times
	 * a record is seen
	 */
	switch (compare_flag) {
	case COMP_ALLOC:
		set_single_cmp(compare_ts, SORT_ASC);
		break;
	case COMP_PAGE_NUM:
		set_single_cmp(compare_page_num, SORT_DESC);
		break;
	case COMP_PID:
		set_single_cmp(compare_pid, SORT_ASC);
		break;
	case COMP_STACK:
		set_single_cmp(compare_stacktrace, SORT_ASC);
		break;
	case COMP_NO_FLAG:
	case COMP_NUM:
		set_single_cmp(compare_num, SORT_DESC);
		break;
	case COMP_TGID:
		set_single_cmp(compare_tgid, SORT_ASC);
		break;
	case COMP_COMM:
		set_single_cmp(compare_comm, SORT_ASC);
		break;
	default:
		usage();
		exit(1);
	}

	fin = fopen(argv[optind], "r");
	fout = fopen(argv[optind + 1], "w");
	if (!fin || !fout) {
		usage();
		perror("open: ");
		exit(1);
	}

	if (!check_regcomp(&order_pattern, "order\\s*([0-9]*),"))
		goto out_order;
	if (!check_regcomp(&pid_pattern, "pid\\s*([0-9]*),"))
		goto out_pid;
	if (!check_regcomp(&tgid_pattern, "tgid\\s*([0-9]*) "))
		goto out_tgid;
	if (!check_regcomp(&comm_pattern, "tgid\\s*[0-9]*\\s*\\((.*)\\),\\s*ts"))
		goto out_comm;
	if (!check_regcomp(&ts_nsec_pattern, "ts\\s*([0-9]*)\\s*ns"))
		goto out_ts;

	fstat(fileno(fin), &st);
	max_size = st.st_size / 100; /* hack ... */

	list = malloc(max_size * sizeof(*list));
	buf = malloc(BUF_SIZE);
	ext_buf = malloc(BUF_SIZE);
	if (!list || !buf || !ext_buf) {
		fprintf(stderr, "Out of memory\n");
		goto out_free;
	}

	for ( ; ; ) {
		int buf_len = read_block(buf, ext_buf, BUF_SIZE, fin);

		if (buf_len < 0)
			break;
		if (!add_list(buf, buf_len, ext_buf))
			goto out_free;
	}

	printf("loaded %d\n", list_size);

	printf("sorting ....\n");

	qsort(list, list_size, sizeof(list[0]), compare_cull_condition);

	printf("culling\n");

	for (i = count = 0; i < list_size; i++) {
		if (count == 0 ||
		    compare_cull_condition((void *)(&list[count-1]), (void *)(&list[i])) != 0) {
			list[count++] = list[i];
		} else {
			list[count-1].num += list[i].num;
			list[count-1].page_num += list[i].page_num;
		}
	}

	qsort(list, count, sizeof(list[0]), compare_sort_condition);

	for (i = 0; i < count; i++) {
		if (cull == 0) {
			fprintf(fout, "%d times, %d pages, ", list[i].num, list[i].page_num);
			print_allocator(fout, list[i].allocator);
			fprintf(fout, ":\n%s\n", list[i].txt);
		}
		else {
			fprintf(fout, "%d times, %d pages",
					list[i].num, list[i].page_num);
			if (cull & CULL_PID || filter & FILTER_PID)
				fprintf(fout, ", PID %d", list[i].pid);
			if (cull & CULL_TGID || filter & FILTER_TGID)
				fprintf(fout, ", TGID %d", list[i].tgid);
			if (cull & CULL_COMM || filter & FILTER_COMM)
				fprintf(fout, ", task_comm_name: %s", list[i].comm);
			if (cull & CULL_ALLOCATOR) {
				fprintf(fout, ", ");
				print_allocator(fout, list[i].allocator);
			}
			if (cull & CULL_STACKTRACE)
				fprintf(fout, ":\n%s", list[i].stacktrace);
			fprintf(fout, "\n");
		}
	}

out_free:
	if (ext_buf)
		free(ext_buf);
	if (buf)
		free(buf);
	if (list)
		free(list);
out_ts:
	regfree(&ts_nsec_pattern);
out_comm:
	regfree(&comm_pattern);
out_tgid:
	regfree(&tgid_pattern);
out_pid:
	regfree(&pid_pattern);
out_order:
	regfree(&order_pattern);

	return 0;
}
