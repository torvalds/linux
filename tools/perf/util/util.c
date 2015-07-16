#include "../perf.h"
#include "util.h"
#include "debug.h"
#include <api/fs/fs.h>
#include <sys/mman.h>
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
#include <unistd.h>
#include "callchain.h"

struct callchain_param	callchain_param = {
	.mode	= CHAIN_GRAPH_REL,
	.min_percent = 0.5,
	.order  = ORDER_CALLEE,
	.key	= CCKEY_FUNCTION
};

/*
 * XXX We need to find a better place for these things...
 */
unsigned int page_size;
int cacheline_size;

bool test_attr__enabled;

bool perf_host  = true;
bool perf_guest = false;

char tracing_events_path[PATH_MAX + 1] = "/sys/kernel/debug/tracing/events";

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
	exit(sig);
}

void get_term_dimensions(struct winsize *ws)
{
	char *s = getenv("LINES");

	if (s != NULL) {
		ws->ws_row = atoi(s);
		s = getenv("COLUMNS");
		if (s != NULL) {
			ws->ws_col = atoi(s);
			if (ws->ws_row && ws->ws_col)
				return;
		}
	}
#ifdef TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, ws) == 0 &&
	    ws->ws_row && ws->ws_col)
		return;
#endif
	ws->ws_row = 25;
	ws->ws_col = 80;
}

void set_term_quiet_input(struct termios *old)
{
	struct termios tc;

	tcgetattr(0, old);
	tc = *old;
	tc.c_lflag &= ~(ICANON | ECHO);
	tc.c_cc[VMIN] = 0;
	tc.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &tc);
}

static void set_tracing_events_path(const char *tracing, const char *mountpoint)
{
	snprintf(tracing_events_path, sizeof(tracing_events_path), "%s/%s%s",
		 mountpoint, tracing, "events");
}

static const char *__perf_tracefs_mount(const char *mountpoint)
{
	const char *mnt;

	mnt = tracefs_mount(mountpoint);
	if (!mnt)
		return NULL;

	set_tracing_events_path("", mnt);

	return mnt;
}

static const char *__perf_debugfs_mount(const char *mountpoint)
{
	const char *mnt;

	mnt = debugfs_mount(mountpoint);
	if (!mnt)
		return NULL;

	set_tracing_events_path("tracing/", mnt);

	return mnt;
}

const char *perf_debugfs_mount(const char *mountpoint)
{
	const char *mnt;

	mnt = __perf_tracefs_mount(mountpoint);
	if (mnt)
		return mnt;

	mnt = __perf_debugfs_mount(mountpoint);

	return mnt;
}

void perf_debugfs_set_path(const char *mntpt)
{
	snprintf(debugfs_mountpoint, strlen(debugfs_mountpoint), "%s", mntpt);
	set_tracing_events_path("tracing/", mntpt);
}

static const char *find_tracefs(void)
{
	const char *path = __perf_tracefs_mount(NULL);

	return path;
}

static const char *find_debugfs(void)
{
	const char *path = __perf_debugfs_mount(NULL);

	if (!path)
		fprintf(stderr, "Your kernel does not support the debugfs filesystem");

	return path;
}

/*
 * Finds the path to the debugfs/tracing
 * Allocates the string and stores it.
 */
const char *find_tracing_dir(void)
{
	const char *tracing_dir = "";
	static char *tracing;
	static int tracing_found;
	const char *debugfs;

	if (tracing_found)
		return tracing;

	debugfs = find_tracefs();
	if (!debugfs) {
		tracing_dir = "/tracing";
		debugfs = find_debugfs();
		if (!debugfs)
			return NULL;
	}

	if (asprintf(&tracing, "%s%s", debugfs, tracing_dir) < 0)
		return NULL;

	tracing_found = 1;
	return tracing;
}

char *get_tracing_file(const char *name)
{
	const char *tracing;
	char *file;

	tracing = find_tracing_dir();
	if (!tracing)
		return NULL;

	if (asprintf(&file, "%s/%s", tracing, name) < 0)
		return NULL;

	return file;
}

void put_tracing_file(char *file)
{
	free(file);
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

int filename__read_str(const char *filename, char **buf, size_t *sizep)
{
	size_t size = 0, alloc_size = 0;
	void *bf = NULL, *nbf;
	int fd, n, err = 0;
	char sbuf[STRERR_BUFSIZE];

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return -errno;

	do {
		if (size == alloc_size) {
			alloc_size += BUFSIZ;
			nbf = realloc(bf, alloc_size);
			if (!nbf) {
				err = -ENOMEM;
				break;
			}

			bf = nbf;
		}

		n = read(fd, bf + size, alloc_size - size);
		if (n < 0) {
			if (size) {
				pr_warning("read failed %d: %s\n", errno,
					 strerror_r(errno, sbuf, sizeof(sbuf)));
				err = 0;
			} else
				err = -errno;

			break;
		}

		size += n;
	} while (n > 0);

	if (!err) {
		*sizep = size;
		*buf   = bf;
	} else
		free(bf);

	close(fd);
	return err;
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
		return -1;

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
