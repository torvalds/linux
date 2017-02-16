#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <lkl.h>
#include <lkl_host.h>
#ifndef __MINGW32__
#include <sys/stat.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#else
#include <windows.h>
#endif

#include "test.h"

static struct cl_args {
	int printk;
	const char *disk_filename;
	const char *tap_ifname;
	const char *fstype;
	int part;
} cla;

static struct cl_option {
	const char *long_name;
	char short_name;
	const char *help;
	int has_arg;
} options[] = {
	{"enable-printk", 'p', "show Linux printks", 0},
	{"disk-file", 'd', "disk file to use", 1},
	{"partition", 'P', "partition to mount", 1},
	{"net-tap", 'n', "tap interface to use", 1},
	{"type", 't', "filesystem type", 1},
	{0},
};

static int parse_opt(int key, char *arg)
{
	switch (key) {
	case 'p':
		cla.printk = 1;
		break;
	case 'P':
		cla.part = atoi(arg);
		break;
	case 'd':
		cla.disk_filename = arg;
		break;
	case 'n':
		cla.tap_ifname = arg;
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

#ifndef __MINGW32__

#define sleep_ns 87654321
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

	if (ret == 0 && delta > sleep_ns * 0.9)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}
#endif

int test_getpid(char *str, int len)
{
	long ret;

	ret = lkl_sys_getpid();

	snprintf(str, len, "%ld", ret);

	if (ret == 1)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

void check_latency(long (*f)(void), long *min, long *max, long *avg)
{
	int i;
	unsigned long long start, stop, sum = 0;
	static const int count = 1000;
	long delta;

	*min = 1000000000;
	*max = -1;

	for (i = 0; i < count; i++) {
		start = lkl_host_ops.time();
		f();
		stop = lkl_host_ops.time();
		delta = stop - start;
		if (*min > delta)
			*min = delta;
		if (*max < delta)
			*max = delta;
		sum += delta;
	}
	*avg = sum / count;
}

static long native_getpid(void)
{
#ifdef __MINGW32__
	GetCurrentProcessId();
#else
	getpid();
#endif
	return 0;
}

int test_syscall_latency(char *str, int len)
{
	long min, max, avg;
	int tmp;

	check_latency(lkl_sys_getpid, &min, &max, &avg);

	tmp = snprintf(str, len, "avg/min/max lkl: %ld/%ld/%ld ",
		       avg, min, max);
	str += tmp;
	len -= tmp;

	check_latency(native_getpid, &min, &max, &avg);

	snprintf(str, len, "native: %ld/%ld/%ld", avg, min, max);

	return TEST_SUCCESS;
}

#define access_rights 0721

int test_creat(char *str, int len)
{
	long ret;

	ret = lkl_sys_creat("/file", access_rights);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_close(char *str, int len)
{
	long ret;

	ret = lkl_sys_close(0);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_failopen(char *str, int len)
{
	long ret;

	ret = lkl_sys_open("/file2", 0, 0);

	snprintf(str, len, "%ld", ret);

	if (ret == -LKL_ENOENT)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_umask(char *str, int len)
{
	long ret, ret2;

	ret = lkl_sys_umask(0777);

	ret2 = lkl_sys_umask(0);

	snprintf(str, len, "%lo %lo", ret, ret2);

	if (ret > 0 && ret2 == 0777)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_open(char *str, int len)
{
	long ret;

	ret = lkl_sys_open("/file", LKL_O_RDWR, 0);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static const char write_test[] = "test";

int test_write(char *str, int len)
{
	long ret;

	ret = lkl_sys_write(0, write_test, sizeof(write_test));

	snprintf(str, len, "%ld", ret);

	if (ret == sizeof(write_test))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_lseek(char *str, int len)
{
	long ret;

	ret = lkl_sys_lseek(0, 0, LKL_SEEK_SET);

	snprintf(str, len, "%zd ", ret);

	if (ret >= 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_read(char *str, int len)
{
	char buf[10] = { 0, };
	long ret;

	ret = lkl_sys_read(0, buf, sizeof(buf));

	snprintf(str, len, "%ld %s", ret, buf);

	if (ret == sizeof(write_test) && strcmp(write_test, buf) == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_fstat(char *str, int len)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_fstat(0, &stat);

	snprintf(str, len, "%ld %o %zd", ret, stat.st_mode, stat.st_size);

	if (ret == 0 && stat.st_size == sizeof(write_test) &&
	    stat.st_mode == (access_rights | LKL_S_IFREG))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_mkdir(char *str, int len)
{
	long ret;

	ret = lkl_sys_mkdir("/mnt", access_rights);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_stat(char *str, int len)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_stat("/mnt", &stat);

	snprintf(str, len, "%ld %o", ret, stat.st_mode);

	if (ret == 0 && stat.st_mode == (access_rights | LKL_S_IFDIR))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static struct lkl_disk disk;
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

	disk.ops = NULL;

	disk_id = lkl_disk_add(&disk);
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
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

#ifndef __MINGW32__
static int netdev_id = -1;

int test_netdev_add(char *str, int len)
{
	struct lkl_netdev *netdev;
	int ret = 0;

	netdev = lkl_netdev_tap_create(cla.tap_ifname, 0);
	if (!netdev)
		goto out;

	ret = lkl_netdev_add((struct lkl_netdev *)netdev, NULL);
	if (ret < 0)
		goto out;

	netdev_id = ret;

out:
	snprintf(str, len, "%d %p %d", ret, netdev, netdev_id);
	return ret >= 0 ? TEST_SUCCESS : TEST_FAILURE;
}

static int test_netdev_ifup(char *str, int len)
{
	long ret;
	int ifindex = -1;

	ret = lkl_netdev_get_ifindex(netdev_id);
	if (ret < 0)
		goto out;
	ifindex = ret;

	ret = lkl_if_up(ifindex);

out:
	snprintf(str, len, "%ld %d", ret, ifindex);

	if (!ret)
		return TEST_SUCCESS;
	return TEST_FAILURE;
}
#endif /* __MINGW32__ */

static int test_pipe2(char *str, int len)
{
	int pipe_fds[2];
	int READ_IDX = 0, WRITE_IDX = 1;
	const char msg[] = "Hello world!";
	int msg_len_bytes = strlen(msg) + 1;
	int cmp_res = 0;
	long ret;

	ret = lkl_sys_pipe2(pipe_fds, LKL_O_NONBLOCK);
	if (ret) {
		snprintf(str, len, "pipe2: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[WRITE_IDX], msg, msg_len_bytes);
	if (ret != msg_len_bytes) {
		if (ret < 0)
			snprintf(str, len, "write: %s", lkl_strerror(ret));
		else
			snprintf(str, len, "write: short write");
		return TEST_FAILURE;
	}

	ret = lkl_sys_read(pipe_fds[READ_IDX], str, msg_len_bytes);
	if (ret != msg_len_bytes) {
		if (ret < 0)
			snprintf(str, len, "read: %s", lkl_strerror(ret));
		else
			snprintf(str, len, "read: short read\n");
		return TEST_FAILURE;
	}

	if ((cmp_res = memcmp(msg, str, msg_len_bytes))) {
		snprintf(str, MAX_MSG_LEN, "%d", cmp_res);
		return TEST_FAILURE;
	}

	ret = lkl_sys_close(pipe_fds[0]);
	if (ret) {
		snprintf(str, len, "close: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_close(pipe_fds[1]);
	if (ret) {
		snprintf(str, len, "close: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

static int test_epoll(char *str, int len)
{
	int epoll_fd, pipe_fds[2];
	int READ_IDX = 0, WRITE_IDX = 1;
	struct lkl_epoll_event wait_on, read_result;
	const char msg[] = "Hello world!";
	long ret;

	memset(&wait_on, 0, sizeof(wait_on));
	memset(&read_result, 0, sizeof(read_result));

	ret = lkl_sys_pipe2(pipe_fds, LKL_O_NONBLOCK);
	if (ret) {
		snprintf(str, len, "pipe2: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	epoll_fd = lkl_sys_epoll_create(1);
	if (epoll_fd < 0) {
		snprintf(str, len, "epoll_create: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	wait_on.events = LKL_POLLIN | LKL_POLLOUT;
	wait_on.data = pipe_fds[READ_IDX];

	ret = lkl_sys_epoll_ctl(epoll_fd, LKL_EPOLL_CTL_ADD, pipe_fds[READ_IDX],
				&wait_on);
	if (ret < 0) {
		snprintf(str, len, "epoll_ctl: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	/* Shouldn't be ready before we have written something */
	ret = lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0);
	if (ret != 0) {
		if (ret < 0)
			snprintf(str, len, "epoll_wait: %s", lkl_strerror(ret));
		else
			snprintf(str, len, "epoll_wait: bad event");
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[WRITE_IDX], msg, strlen(msg) + 1);
	if (ret < 0) {
		snprintf(str, len, "write: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	/* We expect exactly 1 fd to be ready immediately */
	ret = lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0);
	if (ret != 1) {
		if (ret < 0)
			snprintf(str, len, "epoll_wait: %s", lkl_strerror(ret));
		else
			snprintf(str, len, "epoll_wait: bad ev no %ld\n", ret);
		return TEST_FAILURE;
	}

	/* Already tested reading from pipe2 so no need to do it
	 * here */
	snprintf(str, MAX_MSG_LEN, "%s", msg);

	return TEST_SUCCESS;
}

static char mnt_point[32];

static int test_mount_dev(char *str, int len)
{
	long ret;

	ret = lkl_mount_dev(disk_id, cla.part, cla.fstype, 0, NULL, mnt_point,
			    sizeof(mnt_point));

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_chdir(char *str, int len, const char *path)
{
	long ret;

	ret = lkl_sys_chdir(path);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int dir_fd;

static int test_opendir(char *str, int len)
{
	dir_fd = lkl_sys_open(".", LKL_O_RDONLY | LKL_O_DIRECTORY, 0);

	snprintf(str, len, "%d", dir_fd);

	if (dir_fd > 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
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

	if (ret < 0)
		return TEST_FAILURE;

	for (pos = buf; pos - buf < ret; pos += de->d_reclen) {
		de = (struct lkl_linux_dirent64 *)pos;

		wr += snprintf(str + wr, len - wr, "%s ", de->d_name);
		if (wr >= len)
			break;
	}

	return TEST_SUCCESS;
}

static int test_umount_dev(char *str, int len)
{
	long ret, ret2, ret3;

	ret = lkl_sys_close(dir_fd);

	ret2 = lkl_sys_chdir("/");

	ret3 = lkl_umount_dev(disk_id, cla.part, 0, 1000);

	snprintf(str, len, "%ld %ld %ld", ret, ret2, ret3);

	if (!ret && !ret2 && !ret3)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_mount_fs(char *str, int len, char *fs)
{
	long ret;

	ret = lkl_mount_fs(fs);

	snprintf(str, len, "%s: %ld", fs, ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_umount_fs(char *str, int len, char *fs)
{
	long ret, ret2, ret3;

	ret = lkl_sys_close(dir_fd);
	ret2 = lkl_sys_chdir("/");
	ret3 = lkl_umount_timeout(fs, 0, 1000);

	snprintf(str, len, "%s: %ld %ld %ld", fs, ret, ret2, ret3);

	if (!ret && !ret2 && !ret3)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_lo_ifup(char *str, int len)
{
	long ret;

	ret = lkl_if_up(1);

	snprintf(str, len, "%ld", ret);

	if (!ret)
		return TEST_SUCCESS;
	return TEST_FAILURE;
}

static int test_mutex(char *str, int len)
{
	long ret = TEST_SUCCESS;
	/*
	 * Can't do much to verify that this works, so we'll just let Valgrind
	 * warn us on CI if we've made bad memory accesses.
	 */

	struct lkl_mutex *mutex;

	mutex = lkl_host_ops.mutex_alloc(0);
	lkl_host_ops.mutex_lock(mutex);
	lkl_host_ops.mutex_unlock(mutex);
	lkl_host_ops.mutex_free(mutex);

	mutex = lkl_host_ops.mutex_alloc(1);
	lkl_host_ops.mutex_lock(mutex);
	lkl_host_ops.mutex_lock(mutex);
	lkl_host_ops.mutex_unlock(mutex);
	lkl_host_ops.mutex_unlock(mutex);
	lkl_host_ops.mutex_free(mutex);

	snprintf(str, len, "%ld", ret);

	return ret;
}

static int test_semaphore(char *str, int len)
{
	long ret = TEST_SUCCESS;
	/*
	 * Can't do much to verify that this works, so we'll just let Valgrind
	 * warn us on CI if we've made bad memory accesses.
	 */

	struct lkl_sem *sem = lkl_host_ops.sem_alloc(1);
	lkl_host_ops.sem_down(sem);
	lkl_host_ops.sem_up(sem);
	lkl_host_ops.sem_free(sem);

	snprintf(str, len, "%ld", ret);

	return ret;
}

static int test_gettid(char *str, int len)
{
	long tid = lkl_host_ops.gettid();
	snprintf(str, len, "%ld", tid);

	/* As far as I know, thread IDs are non-zero on all reasonable
	 * systems. */
	if (tid) {
		return TEST_SUCCESS;
	} else {
		return TEST_FAILURE;
	}
}

static void test_thread(void *data)
{
	int *pipe_fds = (int*) data;
	char tmp[LKL_PIPE_BUF+1];
	int ret;

	ret = lkl_sys_read(pipe_fds[0], tmp, sizeof(tmp));
	if (ret < 0) {
		fprintf(stderr, "%s: %s\n", __func__, lkl_strerror(ret));
	}

}

static int test_syscall_thread(char *str, int len)
{
	int pipe_fds[2];
	char tmp[LKL_PIPE_BUF+1];
	long ret;
	lkl_thread_t tid;

	ret = lkl_sys_pipe2(pipe_fds, 0);
	if (ret) {
		snprintf(str, len, "pipe2: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_fcntl(pipe_fds[0], LKL_F_SETPIPE_SZ, 1);
	if (ret < 0) {
		snprintf(str, len, "fcntl setpipe_sz: %s", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	tid = lkl_host_ops.thread_create(test_thread, pipe_fds);
	if (!tid) {
		snprintf(str, len, "failed to create thread");
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[1], tmp, sizeof(tmp));
	if (ret != sizeof(tmp)) {
		if (ret < 0)
			snprintf(str, len, "write: %s", lkl_strerror(ret));
		else
			snprintf(str, len, "write: short write: %ld", ret);
		return TEST_FAILURE;
	}

	ret = lkl_host_ops.thread_join(tid);
	if (ret) {
		snprintf(str, len, "failed to join thread");
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

#ifndef __MINGW32__
static void thread_get_pid(void *unused)
{
	lkl_sys_getpid();
}

static int test_many_syscall_threads(char *str, int len)
{
	lkl_thread_t tid;
	int count = 65, ret;

	while (--count > 0) {
		tid = lkl_host_ops.thread_create(thread_get_pid, NULL);
		if (!tid) {
			snprintf(str, len, "failed to create thread");
			return TEST_FAILURE;
		}

		ret = lkl_host_ops.thread_join(tid);
		if (ret) {
			snprintf(str, len, "failed to join thread");
			return TEST_FAILURE;
		}
	}

	return TEST_SUCCESS;
}
#endif

static void thread_quit_immediately(void *unused)
{
}

static int test_join(char *str, int len)
{
	lkl_thread_t tid = lkl_host_ops.thread_create(thread_quit_immediately, NULL);
	int ret = lkl_host_ops.thread_join(tid);

	if (ret == 0) {
		snprintf(str, len, "joined %ld", tid);
		return TEST_SUCCESS;
	} else {
		snprintf(str, len, "failed joining %ld", tid);
		return TEST_FAILURE;
	}
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

	TEST(mutex);
	TEST(semaphore);
	TEST(join);

	if (cla.disk_filename)
		TEST(disk_add);
#ifndef __MINGW32__
	if (cla.tap_ifname)
		TEST(netdev_add);
#endif /* __MINGW32__ */
	lkl_start_kernel(&lkl_host_ops, "mem=16M loglevel=8");

	TEST(getpid);
	TEST(syscall_latency);
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
	if (netdev_id >= 0)
		TEST(netdev_ifup);
#endif  /* __MINGW32__ */
	TEST(pipe2);
	TEST(epoll);
	TEST(mount_fs, "proc");
	TEST(chdir, "proc");
	TEST(opendir);
	TEST(getdents64);
	TEST(umount_fs, "proc");
	if (cla.disk_filename) {
		TEST(mount_dev);
		TEST(chdir, mnt_point);
		TEST(opendir);
		TEST(getdents64);
		TEST(umount_dev);
	}
	TEST(lo_ifup);
	TEST(gettid);
	TEST(syscall_thread);
	/*
	 * Wine has an issue where the FlsCallback is not called when the thread
	 * terminates which makes testing the automatic syscall threads cleanup
	 * impossible under wine.
	 */
#ifndef __MINGW32__
	TEST(many_syscall_threads);
#endif

	lkl_sys_halt();

	lkl_disk_remove(disk);
#ifdef __MINGW32__
	CloseHandle(disk.handle);
#else
	close(disk.fd);
#endif

	return g_test_pass;
}
