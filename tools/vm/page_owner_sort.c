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
 * See Documentation/vm/page_owner.rst
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
	__u64 free_ts_nsec;
	int len;
	int num;
	int page_num;
	pid_t pid;
	pid_t tgid;
};
enum FILTER_BIT {
	FILTER_UNRELEASE = 1<<1,
	FILTER_PID = 1<<2,
	FILTER_TGID = 1<<3,
	FILTER_COMM = 1<<4
};
enum CULL_BIT {
	CULL_UNRELEASE = 1<<1,
	CULL_PID = 1<<2,
	CULL_TGID = 1<<3,
	CULL_COMM = 1<<4,
	CULL_STACKTRACE = 1<<5
};
struct filter_condition {
	pid_t tgid;
	pid_t pid;
	char comm[TASK_COMM_LEN];
};
static struct filter_condition fc;
static regex_t order_pattern;
static regex_t pid_pattern;
static regex_t tgid_pattern;
static regex_t comm_pattern;
static regex_t ts_nsec_pattern;
static regex_t free_ts_nsec_pattern;
static struct block_list *list;
static int list_size;
static int max_size;
static int cull;
static int filter;

int read_block(char *buf, int buf_size, FILE *fin)
{
	char *curr = buf, *const buf_end = buf + buf_size;

	while (buf_end - curr > 1 && fgets(curr, buf_end - curr, fin)) {
		if (*curr == '\n') /* empty line */
			return curr - buf;
		if (!strncmp(curr, "PFN", 3))
			continue;
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

	return l2->num - l1->num;
}

static int compare_page_num(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l2->page_num - l1->page_num;
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

static int compare_free_ts(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	return l1->free_ts_nsec < l2->free_ts_nsec ? -1 : 1;
}


static int compare_release(const void *p1, const void *p2)
{
	const struct block_list *l1 = p1, *l2 = p2;

	if (!l1->free_ts_nsec && !l2->free_ts_nsec)
		return 0;
	if (l1->free_ts_nsec && l2->free_ts_nsec)
		return 0;
	return l1->free_ts_nsec ? 1 : -1;
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
	if ((cull & CULL_UNRELEASE) && compare_release(p1, p2))
		return compare_release(p1, p2);
	return 0;
}

static int search_pattern(regex_t *pattern, char *pattern_str, char *buf)
{
	int err, val_len;
	regmatch_t pmatch[2];

	err = regexec(pattern, buf, 2, pmatch, REG_NOTBOL);
	if (err != 0 || pmatch[1].rm_so == -1) {
		printf("no matching pattern in %s\n", buf);
		return -1;
	}
	val_len = pmatch[1].rm_eo - pmatch[1].rm_so;

	memcpy(pattern_str, buf + pmatch[1].rm_so, val_len);

	return 0;
}

static void check_regcomp(regex_t *pattern, const char *regex)
{
	int err;

	err = regcomp(pattern, regex, REG_EXTENDED | REG_NEWLINE);
	if (err != 0 || pattern->re_nsub != 1) {
		printf("Invalid pattern %s code %d\n", regex, err);
		exit(1);
	}
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
		printf("wrong order in follow buf:\n%s\n", buf);
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
		printf("wrong/invalid pid in follow buf:\n%s\n", buf);
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
		printf("wrong/invalid tgid in follow buf:\n%s\n", buf);
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
		printf("wrong ts_nsec in follow buf:\n%s\n", buf);
		return -1;
	}

	return ts_nsec;
}

static __u64 get_free_ts_nsec(char *buf)
{
	__u64 free_ts_nsec;
	char free_ts_nsec_str[FIELD_BUFF] = {0};
	char *endptr;

	search_pattern(&free_ts_nsec_pattern, free_ts_nsec_str, buf);
	errno = 0;
	free_ts_nsec = strtoull(free_ts_nsec_str, &endptr, 10);
	if (errno != 0 || endptr == free_ts_nsec_str || *endptr != '\0') {
		printf("wrong free_ts_nsec in follow buf:\n%s\n", buf);
		return -1;
	}

	return free_ts_nsec;
}

static char *get_comm(char *buf)
{
	char *comm_str = malloc(TASK_COMM_LEN);

	memset(comm_str, 0, TASK_COMM_LEN);

	search_pattern(&comm_pattern, comm_str, buf);
	errno = 0;
	if (errno != 0) {
		printf("wrong comm in follow buf:\n%s\n", buf);
		return NULL;
	}

	return comm_str;
}

static bool is_need(char *buf)
{
		if ((filter & FILTER_UNRELEASE) && get_free_ts_nsec(buf) != 0)
			return false;
		if ((filter & FILTER_PID) && get_pid(buf) != fc.pid)
			return false;
		if ((filter & FILTER_TGID) && get_tgid(buf) != fc.tgid)
			return false;

		char *comm = get_comm(buf);

		if ((filter & FILTER_COMM) &&
		strncmp(comm, fc.comm, TASK_COMM_LEN) != 0) {
			free(comm);
			return false;
		}
		return true;
}

static void add_list(char *buf, int len)
{
	if (list_size != 0 &&
		len == list[list_size-1].len &&
		memcmp(buf, list[list_size-1].txt, len) == 0) {
		list[list_size-1].num++;
		list[list_size-1].page_num += get_page_num(buf);
		return;
	}
	if (list_size == max_size) {
		printf("max_size too small??\n");
		exit(1);
	}
	if (!is_need(buf))
		return;
	list[list_size].pid = get_pid(buf);
	list[list_size].tgid = get_tgid(buf);
	list[list_size].comm = get_comm(buf);
	list[list_size].txt = malloc(len+1);
	if (!list[list_size].txt) {
		printf("Out of memory\n");
		exit(1);
	}
	memcpy(list[list_size].txt, buf, len);
	list[list_size].txt[len] = 0;
	list[list_size].len = len;
	list[list_size].num = 1;
	list[list_size].page_num = get_page_num(buf);

	list[list_size].stacktrace = strchr(list[list_size].txt, '\n') ?: "";
	if (*list[list_size].stacktrace == '\n')
		list[list_size].stacktrace++;
	list[list_size].ts_nsec = get_ts_nsec(buf);
	list[list_size].free_ts_nsec = get_free_ts_nsec(buf);
	list_size++;
	if (list_size % 1000 == 0) {
		printf("loaded %d\r", list_size);
		fflush(stdout);
	}
}

static bool parse_cull_args(const char *arg_str)
{
	int size = 0;
	char **args = explode(',', arg_str, &size);

	for (int i = 0; i < size; ++i)
		if (!strcmp(args[i], "pid") || !strcmp(args[i], "p"))
			cull |= CULL_PID;
		else if (!strcmp(args[i], "tgid") || !strcmp(args[i], "tg"))
			cull |= CULL_TGID;
		else if (!strcmp(args[i], "name") || !strcmp(args[i], "n"))
			cull |= CULL_COMM;
		else if (!strcmp(args[i], "stacktrace") || !strcmp(args[i], "st"))
			cull |= CULL_STACKTRACE;
		else if (!strcmp(args[i], "free") || !strcmp(args[i], "f"))
			cull |= CULL_UNRELEASE;
		else {
			free_explode(args, size);
			return false;
		}
	free_explode(args, size);
	return true;
}

#define BUF_SIZE	(128 * 1024)

static void usage(void)
{
	printf("Usage: ./page_owner_sort [OPTIONS] <input> <output>\n"
		"-m\t\tSort by total memory.\n"
		"-s\t\tSort by the stack trace.\n"
		"-t\t\tSort by times (default).\n"
		"-p\t\tSort by pid.\n"
		"-P\t\tSort by tgid.\n"
		"-n\t\tSort by task command name.\n"
		"-a\t\tSort by memory allocate time.\n"
		"-r\t\tSort by memory release time.\n"
		"-f\t\tFilter out the information of blocks whose memory has been released.\n"
		"--pid <PID>\tSelect by pid. This selects the information of blocks whose process ID number equals to <PID>.\n"
		"--tgid <TGID>\tSelect by tgid. This selects the information of blocks whose Thread Group ID number equals to <TGID>.\n"
		"--name <command>\n\t\tSelect by command name. This selects the information of blocks whose command name identical to <command>.\n"
		"--cull <rules>\tCull by user-defined rules. <rules> is a single argument in the form of a comma-separated list with some common fields predefined\n"
	);
}

int main(int argc, char **argv)
{
	int (*cmp)(const void *, const void *) = compare_num;
	FILE *fin, *fout;
	char *buf, *endptr;
	int ret, i, count;
	struct stat st;
	int opt;
	struct option longopts[] = {
		{ "pid", required_argument, NULL, 1 },
		{ "tgid", required_argument, NULL, 2 },
		{ "name", required_argument, NULL, 3 },
		{ "cull",  required_argument, NULL, 4 },
		{ 0, 0, 0, 0},
	};

	while ((opt = getopt_long(argc, argv, "afmnprstP", longopts, NULL)) != -1)
		switch (opt) {
		case 'a':
			cmp = compare_ts;
			break;
		case 'f':
			filter = filter | FILTER_UNRELEASE;
			break;
		case 'm':
			cmp = compare_page_num;
			break;
		case 'p':
			cmp = compare_pid;
			break;
		case 'r':
			cmp = compare_free_ts;
			break;
		case 's':
			cmp = compare_stacktrace;
			break;
		case 't':
			cmp = compare_num;
			break;
		case 'P':
			cmp = compare_tgid;
			break;
		case 'n':
			cmp = compare_comm;
			break;
		case 1:
			filter = filter | FILTER_PID;
			errno = 0;
			fc.pid = strtol(optarg, &endptr, 10);
			if (errno != 0 || endptr == optarg || *endptr != '\0') {
				printf("wrong/invalid pid in from the command line:%s\n", optarg);
				exit(1);
			}
			break;
		case 2:
			filter = filter | FILTER_TGID;
			errno = 0;
			fc.tgid = strtol(optarg, &endptr, 10);
			if (errno != 0 || endptr == optarg || *endptr != '\0') {
				printf("wrong/invalid tgid in from the command line:%s\n", optarg);
				exit(1);
			}
			break;
		case 3:
			filter = filter | FILTER_COMM;
			strncpy(fc.comm, optarg, TASK_COMM_LEN);
			fc.comm[TASK_COMM_LEN-1] = '\0';
			break;
		case 4:
			if (!parse_cull_args(optarg)) {
				printf("wrong argument after --cull in from the command line:%s\n",
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

	fin = fopen(argv[optind], "r");
	fout = fopen(argv[optind + 1], "w");
	if (!fin || !fout) {
		usage();
		perror("open: ");
		exit(1);
	}

	check_regcomp(&order_pattern, "order\\s*([0-9]*),");
	check_regcomp(&pid_pattern, "pid\\s*([0-9]*),");
	check_regcomp(&tgid_pattern, "tgid\\s*([0-9]*) ");
	check_regcomp(&comm_pattern, "tgid\\s*[0-9]*\\s*\\((.*)\\),\\s*ts");
	check_regcomp(&ts_nsec_pattern, "ts\\s*([0-9]*)\\s*ns,");
	check_regcomp(&free_ts_nsec_pattern, "free_ts\\s*([0-9]*)\\s*ns");
	fstat(fileno(fin), &st);
	max_size = st.st_size / 100; /* hack ... */

	list = malloc(max_size * sizeof(*list));
	buf = malloc(BUF_SIZE);
	if (!list || !buf) {
		printf("Out of memory\n");
		exit(1);
	}

	for ( ; ; ) {
		ret = read_block(buf, BUF_SIZE, fin);
		if (ret < 0)
			break;

		add_list(buf, ret);
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

	qsort(list, count, sizeof(list[0]), cmp);

	for (i = 0; i < count; i++) {
		if (cull == 0)
			fprintf(fout, "%d times, %d pages:\n%s\n",
					list[i].num, list[i].page_num, list[i].txt);
		else {
			fprintf(fout, "%d times, %d pages",
					list[i].num, list[i].page_num);
			if (cull & CULL_PID || filter & FILTER_PID)
				fprintf(fout, ", PID %d", list[i].pid);
			if (cull & CULL_TGID || filter & FILTER_TGID)
				fprintf(fout, ", TGID %d", list[i].pid);
			if (cull & CULL_COMM || filter & FILTER_COMM)
				fprintf(fout, ", task_comm_name: %s", list[i].comm);
			if (cull & CULL_UNRELEASE)
				fprintf(fout, " (%s)",
						list[i].free_ts_nsec ? "UNRELEASED" : "RELEASED");
			if (cull & CULL_STACKTRACE)
				fprintf(fout, ":\n%s", list[i].stacktrace);
			fprintf(fout, "\n");
		}
	}
	regfree(&order_pattern);
	regfree(&pid_pattern);
	regfree(&tgid_pattern);
	regfree(&comm_pattern);
	regfree(&ts_nsec_pattern);
	regfree(&free_ts_nsec_pattern);
	return 0;
}
