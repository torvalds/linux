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

struct block_list {
	char *txt;
	char *stacktrace;
	int len;
	int num;
	int page_num;
	pid_t pid;
	__u64 ts_nsec;
	__u64 free_ts_nsec;
};

static regex_t order_pattern;
static regex_t pid_pattern;
static regex_t ts_nsec_pattern;
static regex_t free_ts_nsec_pattern;
static struct block_list *list;
static int list_size;
static int max_size;

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
	list[list_size].txt = malloc(len+1);
	list[list_size].len = len;
	list[list_size].num = 1;
	list[list_size].page_num = get_page_num(buf);
	memcpy(list[list_size].txt, buf, len);
	list[list_size].txt[len] = 0;
	list[list_size].stacktrace = strchr(list[list_size].txt, '\n') ?: "";
	list[list_size].pid = get_pid(buf);
	list[list_size].ts_nsec = get_ts_nsec(buf);
	list[list_size].free_ts_nsec = get_free_ts_nsec(buf);
	memcpy(list[list_size].txt, buf, len);
	list[list_size].txt[len] = 0;
	list_size++;
	if (list_size % 1000 == 0) {
		printf("loaded %d\r", list_size);
		fflush(stdout);
	}
}

#define BUF_SIZE	(128 * 1024)

static void usage(void)
{
	printf("Usage: ./page_owner_sort [OPTIONS] <input> <output>\n"
		"-m	Sort by total memory.\n"
		"-s	Sort by the stack trace.\n"
		"-t	Sort by times (default).\n"
		"-p	Sort by pid.\n"
		"-a	Sort by memory allocate time.\n"
		"-r	Sort by memory release time.\n"
		"-c	Cull by comparing stacktrace instead of total block.\n"
		"-f	Filter out the information of blocks whose memory has not been released.\n"
	);
}

int main(int argc, char **argv)
{
	int (*cmp)(const void *, const void *) = compare_num;
	int cull_st = 0;
	int filter = 0;
	FILE *fin, *fout;
	char *buf;
	int ret, i, count;
	struct block_list *list2;
	struct stat st;
	int opt;

	while ((opt = getopt(argc, argv, "acfmprst")) != -1)
		switch (opt) {
		case 'a':
			cmp = compare_ts;
			break;
		case 'c':
			cull_st = 1;
			break;
		case 'f':
			filter = 1;
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

	if (cull_st == 1)
		qsort(list, list_size, sizeof(list[0]), compare_stacktrace);
	else
		qsort(list, list_size, sizeof(list[0]), compare_txt);

	list2 = malloc(sizeof(*list) * list_size);
	if (!list2) {
		printf("Out of memory\n");
		exit(1);
	}

	printf("culling\n");

	long offset = cull_st ? &list[0].stacktrace - &list[0].txt : 0;

	for (i = count = 0; i < list_size; i++) {
		if (count == 0 ||
		    strcmp(*(&list2[count-1].txt+offset), *(&list[i].txt+offset)) != 0) {
			list2[count++] = list[i];
		} else {
			list2[count-1].num += list[i].num;
			list2[count-1].page_num += list[i].page_num;
		}
	}

	qsort(list2, count, sizeof(list[0]), cmp);

	for (i = 0; i < count; i++) {
		if (filter == 1 && list2[i].free_ts_nsec != 0)
			continue;
		fprintf(fout, "%d times, %d pages:\n%s\n",
				list2[i].num, list2[i].page_num, list2[i].txt);
	}
	regfree(&order_pattern);
	regfree(&pid_pattern);
	regfree(&ts_nsec_pattern);
	regfree(&free_ts_nsec_pattern);
	return 0;
}
