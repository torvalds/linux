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

struct block_list {
	char *txt;
	int len;
	int num;
	int page_num;
};

static int sort_by_memory;
static regex_t order_pattern;
static struct block_list *list;
static int list_size;
static int max_size;

struct block_list *block_head;

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

static int get_page_num(char *buf)
{
	int err, val_len, order_val;
	char order_str[4] = {0};
	char *endptr;
	regmatch_t pmatch[2];

	err = regexec(&order_pattern, buf, 2, pmatch, REG_NOTBOL);
	if (err != 0 || pmatch[1].rm_so == -1) {
		printf("no order pattern in %s\n", buf);
		return 0;
	}
	val_len = pmatch[1].rm_eo - pmatch[1].rm_so;
	if (val_len > 2) /* max_order should not exceed 2 digits */
		goto wrong_order;

	memcpy(order_str, buf + pmatch[1].rm_so, val_len);

	errno = 0;
	order_val = strtol(order_str, &endptr, 10);
	if (errno != 0 || endptr == order_str || *endptr != '\0')
		goto wrong_order;

	return 1 << order_val;

wrong_order:
	printf("wrong order in follow buf:\n%s\n", buf);
	return 0;
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
	list_size++;
	if (list_size % 1000 == 0) {
		printf("loaded %d\r", list_size);
		fflush(stdout);
	}
}

#define BUF_SIZE	(128 * 1024)

static void usage(void)
{
	printf("Usage: ./page_owner_sort [-m] <input> <output>\n"
		"-m	Sort by total memory. If this option is unset, sort by times\n"
	);
}

int main(int argc, char **argv)
{
	FILE *fin, *fout;
	char *buf;
	int ret, i, count;
	struct block_list *list2;
	struct stat st;
	int err;
	int opt;

	while ((opt = getopt(argc, argv, "m")) != -1)
		switch (opt) {
		case 'm':
			sort_by_memory = 1;
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

	err = regcomp(&order_pattern, "order\\s*([0-9]*),", REG_EXTENDED|REG_NEWLINE);
	if (err != 0 || order_pattern.re_nsub != 1) {
		printf("%s: Invalid pattern 'order\\s*([0-9]*),' code %d\n",
			argv[0], err);
		exit(1);
	}

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

	qsort(list, list_size, sizeof(list[0]), compare_txt);

	list2 = malloc(sizeof(*list) * list_size);
	if (!list2) {
		printf("Out of memory\n");
		exit(1);
	}

	printf("culling\n");

	for (i = count = 0; i < list_size; i++) {
		if (count == 0 ||
		    strcmp(list2[count-1].txt, list[i].txt) != 0) {
			list2[count++] = list[i];
		} else {
			list2[count-1].num += list[i].num;
			list2[count-1].page_num += list[i].page_num;
		}
	}

	if (sort_by_memory)
		qsort(list2, count, sizeof(list[0]), compare_page_num);
	else
		qsort(list2, count, sizeof(list[0]), compare_num);

	for (i = 0; i < count; i++)
		fprintf(fout, "%d times, %d pages:\n%s\n",
				list2[i].num, list2[i].page_num, list2[i].txt);

	regfree(&order_pattern);
	return 0;
}
