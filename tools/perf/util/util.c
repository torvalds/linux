#include "../perf.h"
#include "util.h"
#include "debug.h"
#include <api/fs/fs.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#ifdef HAVE_BACKTRACE_SUPPORT
#include <execinfo.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <byteswap.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <unistd.h>
#include "callchain.h"
#include "strlist.h"

struct callchain_param	callchain_param = {
	.mode	= CHAIN_GRAPH_ABS,
	.min_percent = 0.5,
	.order  = ORDER_CALLEE,
	.key	= CCKEY_FUNCTION,
	.value	= CCVAL_PERCENT,
};

/*
 * XXX We need to find a better place for these things...
 */
unsigned int page_size;
int cacheline_size;

bool test_attr__enabled;

bool perf_host  = true;
bool perf_guest = false;

void event_attr_init(struct perf_event_attr *attr)
{
	if (!perf_host)
		attr->exclude_host  = 1;
	if (!perf_guest)
		attr->exclude_guest = 1;
	/* to capture ABI version */
	attr->size = sizeof(*attr);
}

int mkdir_p(char *path, mode_t mode)
{
	struct stat st;
	int err;
	char *d = path;

	if (*d != '/')
		return -1;

	if (stat(path, &st) == 0)
		return 0;

	while (*++d == '/');

	while ((d = strchr(d, '/'))) {
		*d = '\0';
		err = stat(path, &st) && mkdir(path, mode);
		*d++ = '/';
		if (err)
			return -1;
		while (*d == '/')
			++d;
	}
	return (stat(path, &st) && mkdir(path, mode)) ? -1 : 0;
}

int rm_rf(char *path)
{
	DIR *dir;
	int ret = 0;
	struct dirent *d;
	char namebuf[PATH_MAX];

	dir = opendir(path);
	if (dir == NULL)
		return 0;

	while ((d = readdir(dir)) != NULL && !ret) {
		struct stat statbuf;

		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		scnprintf(namebuf, sizeof(namebuf), "%s/%s",
			  path, d->d_name);

		ret = stat(namebuf, &statbuf);
		if (ret < 0) {
			pr_debug("stat failed: %s\n", namebuf);
			break;
		}

		if (S_ISREG(statbuf.st_mode))
			ret = unlink(namebuf);
		else if (S_ISDIR(statbuf.st_mode))
			ret = rm_rf(namebuf);
		else {
			pr_debug("unknown file: %s\n", namebuf);
			ret = -1;
		}
	}
	closedir(dir);

	if (ret < 0)
		return ret;

	return rmdir(path);
}

static int slow_copyfile(const char *from, const char *to)
{
	int err = -1;
	char *line = NULL;
	size_t n;
	FILE *from_fp = fopen(from, "r"), *to_fp;

	if (from_fp == NULL)
		goto out;

	to_fp = fopen(to, "w");
	if (to_fp == NULL)
		goto out_fclose_from;

	while (getline(&line, &n, from_fp) > 0)
		if (fputs(line, to_fp) == EOF)
			goto out_fclose_to;
	err = 0;
out_fclose_to:
	fclose(to_fp);
	free(line);
out_fclose_from:
	fclose(from_fp);
out:
	return err;
}

int copyfile_offset(int ifd, loff_t off_in, int ofd, loff_t off_out, u64 size)
{
	void *ptr;
	loff_t pgoff;

	pgoff = off_in & ~(page_size - 1);
	off_in -= pgoff;

	ptr = mmap(NULL, off_in + size, PROT_READ, MAP_PRIVATE, ifd, pgoff);
	if (ptr == MAP_FAILED)
		return -1;

	while (size) {
		ssize_t ret = pwrite(ofd, ptr + off_in, size, off_out);
		if (ret < 0 && errno == EINTR)
			continue;
		if (ret <= 0)
			break;

		size -= ret;
		off_in += ret;
		off_out -= ret;
	}
	munmap(ptr, off_in + size);

	return size ? -1 : 0;
}

int copyfile_mode(const char *from, const char *to, mode_t mode)
{
	int fromfd, tofd;
	struct stat st;
	int err = -1;
	char *tmp = NULL, *ptr = NULL;

	if (stat(from, &st))
		goto out;

	/* extra 'x' at the end is to reserve space for '.' */
	if (asprintf(&tmp, "%s.XXXXXXx", to) < 0) {
		tmp = NULL;
		goto out;
	}
	ptr = strrchr(tmp, '/');
	if (!ptr)
		goto out;
	ptr = memmove(ptr + 1, ptr, strlen(ptr) - 1);
	*ptr = '.';

	tofd = mkstemp(tmp);
	if (tofd < 0)
		goto out;

	if (fchmod(tofd, mode))
		goto out_close_to;

	if (st.st_size == 0) { /* /proc? do it slowly... */
		err = slow_copyfile(from, tmp);
		goto out_close_to;
	}

	fromfd = open(from, O_RDONLY);
	if (fromfd < 0)
		goto out_close_to;

	err = copyfile_offset(fromfd, 0, tofd, 0, st.st_size);

	close(fromfd);
out_close_to:
	close(tofd);
	if (!err)
		err = link(tmp, to);
	unlink(tmp);
out:
	free(tmp);
	return err;
}

int copyfile(const char *from, const char *to)
{
	return copyfile_mode(from, to, 0755);
}

unsigned long convert_unit(unsigned long value, char *unit)
{
	*unit = ' ';

	if (value > 1000) {
		value /= 1000;
		*unit = 'K';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'M';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'G';
	}

	return value;
}

static ssize_t ion(bool is_read, int fd, void *buf, size_t n)
{
	void *buf_start = buf;
	size_t left = n;

	while (left) {
		ssize_t ret = is_read ? read(fd, buf, left) :
					write(fd, buf, left);

		if (ret < 0 && errno == EINTR)
			continue;
		if (ret <= 0)
			return ret;

		left -= ret;
		buf  += ret;
	}

	BUG_ON((size_t)(buf - buf_start) != n);
	return n;
}

/*
 * Read exactly 'n' bytes or return an error.
 */
ssize_t readn(int fd, void *buf, size_t n)
{
	return ion(true, fd, buf, n);
}

/*
 * Write exactly 'n' bytes or return an error.
 */
ssize_t writen(int fd, void *buf, size_t n)
{
	return ion(false, fd, buf, n);
}

size_t hex_width(u64 v)
{
	size_t n = 1;

	while ((v >>= 4))
		++n;

	return n;
}

static int hex(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	return -1;
}

/*
 * While we find nice hex chars, build a long_val.
 * Return number of chars processed.
 */
int hex2u64(const char *ptr, u64 *long_val)
{
	const char *p = ptr;
	*long_val = 0;

	while (*p) {
		const int hex_val = hex(*p);

		if (hex_val < 0)
			break;

		*long_val = (*long_val << 4) | hex_val;
		p++;
	}

	return p - ptr;
}

/* Obtain a backtrace and print it to stdout. */
#ifdef HAVE_BACKTRACE_SUPPORT
void dump_stack(void)
{
	void *array[16];
	size_t size = backtrace(array, ARRAY_SIZE(array));
	char **strings = backtrace_symbols(array, size);
	size_t i;

	printf("Obtained %zd stack frames.\n", size);

	for (i = 0; i < size; i++)
		printf("%s\n", strings[i]);

	free(strings);
}
#else
void dump_stack(void) {}
#endif

void sighandler_dump_stack(int sig)
{
	psignal(sig, "perf");
	dump_stack();
	signal(sig, SIG_DFL);
	raise(sig);
}

int parse_nsec_time(const char *str, u64 *ptime)
{
	u64 time_sec, time_nsec;
	char *end;

	time_sec = strtoul(str, &end, 10);
	if (*end != '.' && *end != '\0')
		return -1;

	if (*end == '.') {
		int i;
		char nsec_buf[10];

		if (strlen(++end) > 9)
			return -1;

		strncpy(nsec_buf, end, 9);
		nsec_buf[9] = '\0';

		/* make it nsec precision */
		for (i = strlen(nsec_buf); i < 9; i++)
			nsec_buf[i] = '0';

		time_nsec = strtoul(nsec_buf, &end, 10);
		if (*end != '\0')
			return -1;
	} else
		time_nsec = 0;

	*ptime = time_sec * NSEC_PER_SEC + time_nsec;
	return 0;
}

unsigned long parse_tag_value(const char *str, struct parse_tag *tags)
{
	struct parse_tag *i = tags;

	while (i->tag) {
		char *s;

		s = strchr(str, i->tag);
		if (s) {
			unsigned long int value;
			char *endptr;

			value = strtoul(str, &endptr, 10);
			if (s != endptr)
				break;

			if (value > ULONG_MAX / i->mult)
				break;
			value *= i->mult;
			return value;
		}
		i++;
	}

	return (unsigned long) -1;
}

int get_stack_size(const char *str, unsigned long *_size)
{
	char *endptr;
	unsigned long size;
	unsigned long max_size = round_down(USHRT_MAX, sizeof(u64));

	size = strtoul(str, &endptr, 0);

	do {
		if (*endptr)
			break;

		size = round_up(size, sizeof(u64));
		if (!size || size > max_size)
			break;

		*_size = size;
		return 0;

	} while (0);

	pr_err("callchain: Incorrect stack dump size (max %ld): %s\n",
	       max_size, str);
	return -1;
}

int parse_callchain_record(const char *arg, struct callchain_param *param)
{
	char *tok, *name, *saveptr = NULL;
	char *buf;
	int ret = -1;

	/* We need buffer that we know we can write to. */
	buf = malloc(strlen(arg) + 1);
	if (!buf)
		return -ENOMEM;

	strcpy(buf, arg);

	tok = strtok_r((char *)buf, ",", &saveptr);
	name = tok ? : (char *)buf;

	do {
		/* Framepointer style */
		if (!strncmp(name, "fp", sizeof("fp"))) {
			if (!strtok_r(NULL, ",", &saveptr)) {
				param->record_mode = CALLCHAIN_FP;
				ret = 0;
			} else
				pr_err("callchain: No more arguments "
				       "needed for --call-graph fp\n");
			break;

#ifdef HAVE_DWARF_UNWIND_SUPPORT
		/* Dwarf style */
		} else if (!strncmp(name, "dwarf", sizeof("dwarf"))) {
			const unsigned long default_stack_dump_size = 8192;

			ret = 0;
			param->record_mode = CALLCHAIN_DWARF;
			param->dump_size = default_stack_dump_size;

			tok = strtok_r(NULL, ",", &saveptr);
			if (tok) {
				unsigned long size = 0;

				ret = get_stack_size(tok, &size);
				param->dump_size = size;
			}
#endif /* HAVE_DWARF_UNWIND_SUPPORT */
		} else if (!strncmp(name, "lbr", sizeof("lbr"))) {
			if (!strtok_r(NULL, ",", &saveptr)) {
				param->record_mode = CALLCHAIN_LBR;
				ret = 0;
			} else
				pr_err("callchain: No more arguments "
					"needed for --call-graph lbr\n");
			break;
		} else {
			pr_err("callchain: Unknown --call-graph option "
			       "value: %s\n", arg);
			break;
		}

	} while (0);

	free(buf);
	return ret;
}

const char *get_filename_for_perf_kvm(void)
{
	const char *filename;

	if (perf_host && !perf_guest)
		filename = strdup("perf.data.host");
	else if (!perf_host && perf_guest)
		filename = strdup("perf.data.guest");
	else
		filename = strdup("perf.data.kvm");

	return filename;
}

int perf_event_paranoid(void)
{
	int value;

	if (sysctl__read_int("kernel/perf_event_paranoid", &value))
		return INT_MAX;

	return value;
}

void mem_bswap_32(void *src, int byte_size)
{
	u32 *m = src;
	while (byte_size > 0) {
		*m = bswap_32(*m);
		byte_size -= sizeof(u32);
		++m;
	}
}

void mem_bswap_64(void *src, int byte_size)
{
	u64 *m = src;

	while (byte_size > 0) {
		*m = bswap_64(*m);
		byte_size -= sizeof(u64);
		++m;
	}
}

bool find_process(const char *name)
{
	size_t len = strlen(name);
	DIR *dir;
	struct dirent *d;
	int ret = -1;

	dir = opendir(procfs__mountpoint());
	if (!dir)
		return false;

	/* Walk through the directory. */
	while (ret && (d = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		char *data;
		size_t size;

		if ((d->d_type != DT_DIR) ||
		     !strcmp(".", d->d_name) ||
		     !strcmp("..", d->d_name))
			continue;

		scnprintf(path, sizeof(path), "%s/%s/comm",
			  procfs__mountpoint(), d->d_name);

		if (filename__read_str(path, &data, &size))
			continue;

		ret = strncmp(name, data, len);
		free(data);
	}

	closedir(dir);
	return ret ? false : true;
}

int
fetch_kernel_version(unsigned int *puint, char *str,
		     size_t str_size)
{
	struct utsname utsname;
	int version, patchlevel, sublevel, err;

	if (uname(&utsname))
		return -1;

	if (str && str_size) {
		strncpy(str, utsname.release, str_size);
		str[str_size - 1] = '\0';
	}

	err = sscanf(utsname.release, "%d.%d.%d",
		     &version, &patchlevel, &sublevel);

	if (err != 3) {
		pr_debug("Unablt to get kernel version from uname '%s'\n",
			 utsname.release);
		return -1;
	}

	if (puint)
		*puint = (version << 16) + (patchlevel << 8) + sublevel;
	return 0;
}

const char *perf_tip(const char *dirpath)
{
	struct strlist *tips;
	struct str_node *node;
	char *tip = NULL;
	struct strlist_config conf = {
		.dirname = dirpath,
		.file_only = true,
	};

	tips = strlist__new("tips.txt", &conf);
	if (tips == NULL)
		return errno == ENOENT ? NULL : "Tip: get more memory! ;-p";

	if (strlist__nr_entries(tips) == 0)
		goto out;

	node = strlist__entry(tips, random() % strlist__nr_entries(tips));
	if (asprintf(&tip, "Tip: %s", node->s) < 0)
		tip = (char *)"Tip: get more memory! ;-)";

out:
	strlist__delete(tips);

	return tip;
}

bool is_regular_file(const char *file)
{
	struct stat st;

	if (stat(file, &st))
		return false;

	return S_ISREG(st.st_mode);
}

int fetch_current_timestamp(char *buf, size_t sz)
{
	struct timeval tv;
	struct tm tm;
	char dt[32];

	if (gettimeofday(&tv, NULL) || !localtime_r(&tv.tv_sec, &tm))
		return -1;

	if (!strftime(dt, sizeof(dt), "%Y%m%d%H%M%S", &tm))
		return -1;

	scnprintf(buf, sz, "%s%02u", dt, (unsigned)tv.tv_usec / 10000);

	return 0;
}

void print_binary(unsigned char *data, size_t len,
		  size_t bytes_per_line, print_binary_t printer,
		  void *extra)
{
	size_t i, j, mask;

	if (!printer)
		return;

	bytes_per_line = roundup_pow_of_two(bytes_per_line);
	mask = bytes_per_line - 1;

	printer(BINARY_PRINT_DATA_BEGIN, 0, extra);
	for (i = 0; i < len; i++) {
		if ((i & mask) == 0) {
			printer(BINARY_PRINT_LINE_BEGIN, -1, extra);
			printer(BINARY_PRINT_ADDR, i, extra);
		}

		printer(BINARY_PRINT_NUM_DATA, data[i], extra);

		if (((i & mask) == mask) || i == len - 1) {
			for (j = 0; j < mask-(i & mask); j++)
				printer(BINARY_PRINT_NUM_PAD, -1, extra);

			printer(BINARY_PRINT_SEP, i, extra);
			for (j = i & ~mask; j <= i; j++)
				printer(BINARY_PRINT_CHAR_DATA, data[j], extra);
			for (j = 0; j < mask-(i & mask); j++)
				printer(BINARY_PRINT_CHAR_PAD, i, extra);
			printer(BINARY_PRINT_LINE_END, -1, extra);
		}
	}
	printer(BINARY_PRINT_DATA_END, -1, extra);
}
