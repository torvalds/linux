#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#ifndef __MINGW32__
#include <argp.h>
#endif
#include <lkl.h>
#include <lkl_host.h>
#ifndef __MINGW32__
#include <sys/stat.h>
#include <fcntl.h>
#else
#include <windows.h>
#endif

static struct cl_args {
	int printk;
	const char *disk_filename;
	const char *fstype;
} cla;

static struct cl_option {
	const char *long_name;
	char short_name;
	const char *help;
	int has_arg;
} options[] = {
	{"enable-printk", 'p', "show Linux printks", 0},
	{"disk-file", 'd', "disk file to use", 1},
	{"type", 't', "filesystem type", 1},
	{0},
};

static int parse_opt(int key, char *arg)
{
	switch (key) {
	case 'p':
		cla.printk = 1;
		break;
	case 'd':
		cla.disk_filename = arg;
		break;
	case 't':
		cla.fstype = arg;
		break;
	default:
		return -1;
	}

	return 0;
}

void printk(const char *str, int len)
{
	int ret __attribute__((unused));

	if (cla.printk)
		ret = write(STDOUT_FILENO, str, len);
}

static int g_test_pass = 0;
#define TEST(name) {				\
	int ret = do_test(#name, test_##name);	\
	if (!ret) g_test_pass = -1;		\
	}

static int do_test(char *name, int (*fn)(char *, int))
{
	char str[60];
	int result;

	result = fn(str, sizeof(str));
	printf("%-20s %s [%s]\n", name, result ? "passed" : "failed", str);
	return result;
}

#define sleep_ns 87654321

#ifndef __MINGW32__
int test_nanosleep(char *str, int len)
{
	struct lkl_timespec ts = {
		.tv_sec = 0,
		.tv_nsec = sleep_ns,
	};
	struct timespec start, stop;
	long delta;
	long ret;

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = lkl_sys_nanosleep(&ts, NULL);
	clock_gettime(CLOCK_MONOTONIC, &stop);

	delta = 1e9*(stop.tv_sec - start.tv_sec) +
		(stop.tv_nsec - start.tv_nsec);

	snprintf(str, len, "%ld", delta);

	if (ret == 0 && delta > sleep_ns * 0.9 && delta < sleep_ns * 1.1)
		return 1;

	return 0;
}
#endif

int test_getpid(char *str, int len)
{
	long ret;

	ret = lkl_sys_getpid();

	snprintf(str, len, "%ld", ret);

	if (ret == 1)
		return 1;

	return 0;
}

#define access_rights 0721

int test_creat(char *str, int len)
{
	long ret;

	ret = lkl_sys_creat("/file", access_rights);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return 1;

	return 0;
}

int test_close(char *str, int len)
{
	long ret;

	ret = lkl_sys_close(0);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return 1;

	return 0;
}

int test_failopen(char *str, int len)
{
	long ret;

	ret = lkl_sys_open("/file2", 0, 0);

	snprintf(str, len, "%ld", ret);

	if (ret == -LKL_ENOENT)
		return 1;

	return 0;
}

int test_umask(char *str, int len)
{
	long ret, ret2;

	ret = lkl_sys_umask(0777);

	ret2 = lkl_sys_umask(0);

	snprintf(str, len, "%lo %lo", ret, ret2);

	if (ret > 0 && ret2 == 0777)
		return 1;

	return 0;
}

int test_open(char *str, int len)
{
	long ret;

	ret = lkl_sys_open("/file", LKL_O_RDWR, 0);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return 1;

	return 0;
}

static const char write_test[] = "test";

int test_write(char *str, int len)
{
	long ret;

	ret = lkl_sys_write(0, write_test, sizeof(write_test));

	snprintf(str, len, "%ld", ret);

	if (ret == sizeof(write_test))
		return 1;

	return 0;
}

int test_lseek(char *str, int len)
{
	long ret;

	ret = lkl_sys_lseek(0, 0, LKL_SEEK_SET);

	snprintf(str, len, "%zd ", ret);

	if (ret >= 0)
		return 1;

	return 0;
}

int test_read(char *str, int len)
{
	char buf[10] = { 0, };
	long ret;

	ret = lkl_sys_read(0, buf, sizeof(buf));

	snprintf(str, len, "%ld %s", ret, buf);

	if (ret == sizeof(write_test) && strcmp(write_test, buf) == 0)
		return 1;

	return 0;
}

int test_fstat(char *str, int len)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_fstat(0, &stat);

	snprintf(str, len, "%ld %o %zd", ret, stat.st_mode, stat.st_size);

	if (ret == 0 && stat.st_size == sizeof(write_test) &&
	    stat.st_mode == (access_rights | LKL_S_IFREG))
		return 1;

	return 0;
}

int test_mkdir(char *str, int len)
{
	long ret;

	ret = lkl_sys_mkdir("/mnt", access_rights);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return 1;

	return 0;
}

int test_stat(char *str, int len)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_stat("/mnt", &stat);

	snprintf(str, len, "%ld %o", ret, stat.st_mode);

	if (ret == 0 && stat.st_mode == (access_rights | LKL_S_IFDIR))
		return 1;

	return 0;
}

static const char *tmp_file;
static union lkl_disk disk;
static int disk_id = -1;

int test_disk_add(char *str, int len)
{
#ifdef __MINGW32__
	disk.handle = CreateFile(cla.disk_filename, GENERIC_READ | GENERIC_WRITE,
			       0, NULL, OPEN_EXISTING, 0, NULL);
	if (!disk.handle)
#else
	disk.fd = open(cla.disk_filename, O_RDWR);
	if (disk.fd < 0)
#endif
		goto out_unlink;

	disk_id = lkl_disk_add(disk);
	if (disk_id < 0)
		goto out_close;

	goto out;

out_close:
#ifdef __MINGW32__
	CloseHandle(disk.handle);
#else
	close(disk.fd);
#endif

out_unlink:
#ifdef __MINGW32__
	DeleteFile(cla.disk_filename);
#else
	unlink(cla.disk_filename);
#endif

out:
	snprintf(str, len, "%x %d", disk.fd, disk_id);

	if (disk_id >= 0)
		return 1;

	return 0;
}

static char mnt_point[32];

static int test_mount(char *str, int len)
{
	long ret;

	ret = lkl_mount_dev(disk_id, cla.fstype, 0, NULL, mnt_point,
			    sizeof(mnt_point));

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return 1;

	return 0;
}

static int test_chdir(char *str, int len)
{
	long ret;

	ret = lkl_sys_chdir(mnt_point);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return 1;

	return 0;
}

static int dir_fd;

static int test_opendir(char *str, int len)
{
	dir_fd = lkl_sys_open(".", LKL_O_RDONLY | LKL_O_DIRECTORY, 0);

	snprintf(str, len, "%d", dir_fd);

	if (dir_fd > 0)
		return 1;

	return 0;
}

static int test_getdents64(char *str, int len)
{
	long ret;
	char buf[1024], *pos;
	struct lkl_linux_dirent64 *de;
	int wr;

	de = (struct lkl_linux_dirent64 *)buf;
	ret = lkl_sys_getdents64(dir_fd, de, sizeof(buf));

	wr = snprintf(str, len, "%d ", dir_fd);
	str += wr;
	len -= wr;

	if (ret < 0)
		return 0;

	for (pos = buf; pos - buf < ret; pos += de->d_reclen) {
		de = (struct lkl_linux_dirent64 *)pos;

		wr = snprintf(str, len, "%s ", de->d_name);
		str += wr;
		len -= wr;
	}

	return 1;
}

static int test_umount(char *str, int len)
{
	long ret, ret2, ret3;

	ret = lkl_sys_close(dir_fd);

	ret2 = lkl_sys_chdir("/");

	ret3 = lkl_umount_dev(disk_id, 0, 1000);

	snprintf(str, len, "%ld %ld %ld", ret, ret2, ret3);

	if (!ret && !ret2 && !ret3)
		return 1;

	return 0;
}

static struct cl_option *find_short_opt(char name)
{
	struct cl_option *opt;

	for (opt = options; opt->short_name != 0; opt++) {
		if (opt->short_name == name)
			return opt;
	}

	return NULL;
}

static struct cl_option *find_long_opt(const char *name)
{
	struct cl_option *opt;

	for (opt = options; opt->long_name; opt++) {
		if (strcmp(opt->long_name, name) == 0)
			return opt;
	}

	return NULL;
}

static void print_help(void)
{
	struct cl_option *opt;

	printf("usage:\n");
	for (opt = options; opt->long_name; opt++)
		printf("-%c, --%-20s %s\n", opt->short_name, opt->long_name,
		       opt->help);
}

static int parse_opts(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		struct cl_option *opt = NULL;

		if (argv[i][0] == '-') {
			if (argv[i][1] != '-')
				opt = find_short_opt(argv[i][1]);
			else
				opt = find_long_opt(&argv[i][2]);
		}

		if (!opt) {
			print_help();
			return -1;
		}

		if (parse_opt(opt->short_name, argv[i + 1]) < 0) {
			print_help();
			return -1;
		}

		if (opt->has_arg)
			i++;
	}

	return 0;
}

int main(int argc, char **argv)
{
	if (parse_opts(argc, argv) < 0)
		return -1;

	lkl_host_ops.print = printk;

	TEST(disk_add);

	lkl_start_kernel(&lkl_host_ops, 16 * 1024 * 1024, "");

	TEST(getpid);
	TEST(umask);
	TEST(creat);
	TEST(close);
	TEST(failopen);
	TEST(open);
	TEST(write);
	TEST(lseek);
	TEST(read);
	TEST(fstat);
	TEST(mkdir);
	TEST(stat);
#ifndef __MINGW32__
	TEST(nanosleep);
#endif
	TEST(mount);
	TEST(chdir);
	TEST(opendir);
	TEST(getdents64);
	TEST(umount);

	lkl_sys_halt();

	close(disk.fd);
	unlink(tmp_file);

	return g_test_pass;
}
