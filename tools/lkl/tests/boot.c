#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <lkl.h>
#include <lkl_host.h>

#include <sys/stat.h>
#include <fcntl.h>
#if defined(__FreeBSD__)
#include <net/if.h>
#include <sys/ioctl.h>
#elif __linux
#include <sys/epoll.h>
#include <sys/ioctl.h>
#elif __MINGW32__
#include <windows.h>
#endif

#include <lkl/linux/mman.h>

#include "test.h"

#ifndef __MINGW32__
#define sleep_ns 87654321
int lkl_test_nanosleep(void)
{
	struct __lkl__kernel_timespec ts = {
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

	lkl_test_logf("sleep %ld (ret=%ld), expected sleep %d\n",
		      delta, ret, sleep_ns);

	if (ret == 0 && delta > sleep_ns * 0.9)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}
#endif

LKL_TEST_CALL(getpid, lkl_sys_getpid, 1)

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

int lkl_test_syscall_latency(void)
{
	long min, max, avg;

	lkl_test_logf("avg/min/max: ");

	check_latency(lkl_sys_getpid, &min, &max, &avg);

	lkl_test_logf("lkl:%ld/%ld/%ld ", avg, min, max);

	check_latency(native_getpid, &min, &max, &avg);

	lkl_test_logf("native:%ld/%ld/%ld\n", avg, min, max);

	return TEST_SUCCESS;
}

#define access_rights 0721

LKL_TEST_CALL(creat, lkl_sys_creat, 0, "/file", access_rights)
LKL_TEST_CALL(close, lkl_sys_close, 0, 0);
LKL_TEST_CALL(failopen, lkl_sys_open, -LKL_ENOENT, "/file2", 0, 0);
LKL_TEST_CALL(umask, lkl_sys_umask, 022,  0777);
LKL_TEST_CALL(umask2, lkl_sys_umask, 0777, 0);
LKL_TEST_CALL(open, lkl_sys_open, 0, "/file", LKL_O_RDWR, 0);
static const char wrbuf[] = "test";
LKL_TEST_CALL(write, lkl_sys_write, sizeof(wrbuf), 0, wrbuf, sizeof(wrbuf));
LKL_TEST_CALL(lseek_cur, lkl_sys_lseek, sizeof(wrbuf), 0, 0, LKL_SEEK_CUR);
LKL_TEST_CALL(lseek_end, lkl_sys_lseek, sizeof(wrbuf), 0, 0, LKL_SEEK_END);
LKL_TEST_CALL(lseek_set, lkl_sys_lseek, 0, 0, 0, LKL_SEEK_SET);

int lkl_test_read(void)
{
	char buf[10] = { 0, };
	long ret;

	ret = lkl_sys_read(0, buf, sizeof(buf));

	lkl_test_logf("lkl_sys_read=%ld buf=%s\n", ret, buf);

	if (ret == sizeof(wrbuf) && !strcmp(wrbuf, buf))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int lkl_test_fstat(void)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_fstat(0, &stat);

	lkl_test_logf("lkl_sys_fstat=%ld mode=%o size=%zd\n", ret, stat.st_mode,
		      stat.st_size);

	if (ret == 0 && stat.st_size == sizeof(wrbuf) &&
	    stat.st_mode == (access_rights | LKL_S_IFREG))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

LKL_TEST_CALL(mkdir, lkl_sys_mkdir, 0, "/mnt", access_rights)

int lkl_test_stat(void)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_stat("/mnt", &stat);

	lkl_test_logf("lkl_sys_stat(\"/mnt\")=%ld mode=%o\n", ret,
		      stat.st_mode);

	if (ret == 0 && stat.st_mode == (access_rights | LKL_S_IFDIR))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int lkl_test_pipe2(void)
{
	int pipe_fds[2];
	int READ_IDX = 0, WRITE_IDX = 1;
	const char msg[] = "Hello world!";
	char str[20];
	int msg_len_bytes = strlen(msg) + 1;
	int cmp_res;
	long ret;

	ret = lkl_sys_pipe2(pipe_fds, LKL_O_NONBLOCK);
	if (ret) {
		lkl_test_logf("pipe2: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[WRITE_IDX], msg, msg_len_bytes);
	if (ret != msg_len_bytes) {
		if (ret < 0)
			lkl_test_logf("write error: %s\n", lkl_strerror(ret));
		else
			lkl_test_logf("short write: %ld\n", ret);
		return TEST_FAILURE;
	}

	ret = lkl_sys_read(pipe_fds[READ_IDX], str, msg_len_bytes);
	if (ret != msg_len_bytes) {
		if (ret < 0)
			lkl_test_logf("read error: %s\n", lkl_strerror(ret));
		else
			lkl_test_logf("short read: %ld\n", ret);
		return TEST_FAILURE;
	}

	cmp_res = memcmp(msg, str, msg_len_bytes);
	if (cmp_res) {
		lkl_test_logf("memcmp failed: %d\n", cmp_res);
		return TEST_FAILURE;
	}

	ret = lkl_sys_close(pipe_fds[0]);
	if (ret) {
		lkl_test_logf("close error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_close(pipe_fds[1]);
	if (ret) {
		lkl_test_logf("close error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

static int lkl_test_epoll(void)
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
		lkl_test_logf("pipe2 error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	epoll_fd = lkl_sys_epoll_create(1);
	if (epoll_fd < 0) {
		lkl_test_logf("epoll_create error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	wait_on.events = LKL_POLLIN | LKL_POLLOUT;
	wait_on.data = pipe_fds[READ_IDX];

	ret = lkl_sys_epoll_ctl(epoll_fd, LKL_EPOLL_CTL_ADD, pipe_fds[READ_IDX],
				&wait_on);
	if (ret < 0) {
		lkl_test_logf("epoll_ctl error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	/* Shouldn't be ready before we have written something */
	ret = lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0);
	if (ret != 0) {
		if (ret < 0)
			lkl_test_logf("epoll_wait error: %s\n",
				      lkl_strerror(ret));
		else
			lkl_test_logf("epoll_wait: bad event: 0x%lx\n", ret);
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[WRITE_IDX], msg, strlen(msg) + 1);
	if (ret < 0) {
		lkl_test_logf("write error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	/* We expect exactly 1 fd to be ready immediately */
	ret = lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0);
	if (ret != 1) {
		if (ret < 0)
			lkl_test_logf("epoll_wait error: %s\n",
				      lkl_strerror(ret));
		else
			lkl_test_logf("epoll_wait: bad ev no %ld\n", ret);
		return TEST_FAILURE;
	}

	/* Already tested reading from pipe2 so no need to do it
	 * here */

	return TEST_SUCCESS;
}

LKL_TEST_CALL(chdir_proc, lkl_sys_chdir, 0, "proc");

static int dir_fd;

static int lkl_test_open_cwd(void)
{
	dir_fd = lkl_sys_open(".", LKL_O_RDONLY | LKL_O_DIRECTORY, 0);
	if (dir_fd < 0) {
		lkl_test_logf("failed to open current directory: %s\n",
			      lkl_strerror(dir_fd));
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

/* column where to insert a line break for the list file tests below. */
#define COL_LINE_BREAK 70

static int lkl_test_getdents64(void)
{
	long ret;
	char buf[1024], *pos;
	struct lkl_linux_dirent64 *de;
	int wr;

	de = (struct lkl_linux_dirent64 *)buf;
	ret = lkl_sys_getdents64(dir_fd, de, sizeof(buf));

	wr = lkl_test_logf("%d ", dir_fd);

	if (ret < 0)
		return TEST_FAILURE;

	for (pos = buf; pos - buf < ret; pos += de->d_reclen) {
		de = (struct lkl_linux_dirent64 *)pos;

		wr += lkl_test_logf("%s ", de->d_name);
		if (wr >= COL_LINE_BREAK) {
			lkl_test_logf("\n");
			wr = 0;
		}
	}

	return TEST_SUCCESS;
}

LKL_TEST_CALL(close_dir_fd, lkl_sys_close, 0, dir_fd);
LKL_TEST_CALL(chdir_root, lkl_sys_chdir, 0, "/");
LKL_TEST_CALL(mount_fs_proc, lkl_mount_fs, 0, "proc");
LKL_TEST_CALL(umount_fs_proc, lkl_umount_timeout, 0, "proc", 0, 1000);
LKL_TEST_CALL(lo_ifup, lkl_if_up, 0, 1);

static int lkl_test_mutex(void)
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

	return ret;
}

static int lkl_test_semaphore(void)
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

	return ret;
}

static void test_thread(void *data)
{
	int *pipe_fds = (int*) data;
	char tmp[LKL_PIPE_BUF+1];
	int ret;

	ret = lkl_sys_read(pipe_fds[0], tmp, sizeof(tmp));
	if (ret < 0)
		lkl_test_logf("%s: %s\n", __func__, lkl_strerror(ret));
}

static int lkl_test_syscall_thread(void)
{
	int pipe_fds[2];
	char tmp[LKL_PIPE_BUF+1];
	long ret;
	lkl_thread_t tid;

	ret = lkl_sys_pipe2(pipe_fds, 0);
	if (ret) {
		lkl_test_logf("pipe2: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_fcntl(pipe_fds[0], LKL_F_SETPIPE_SZ, 1);
	if (ret < 0) {
		lkl_test_logf("fcntl setpipe_sz: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	tid = lkl_host_ops.thread_create(test_thread, pipe_fds);
	if (!tid) {
		lkl_test_logf("failed to create thread\n");
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[1], tmp, sizeof(tmp));
	if (ret != sizeof(tmp)) {
		if (ret < 0)
			lkl_test_logf("write error: %s\n", lkl_strerror(ret));
		else
			lkl_test_logf("short write: %ld\n", ret);
		return TEST_FAILURE;
	}

	ret = lkl_host_ops.thread_join(tid);
	if (ret) {
		lkl_test_logf("failed to join thread\n");
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

#ifndef __MINGW32__
static void thread_get_pid(void *unused)
{
	lkl_sys_getpid();
}

static int lkl_test_many_syscall_threads(void)
{
	lkl_thread_t tid;
	int count = 65, ret;

	while (--count > 0) {
		tid = lkl_host_ops.thread_create(thread_get_pid, NULL);
		if (!tid) {
			lkl_test_logf("failed to create thread\n");
			return TEST_FAILURE;
		}

		ret = lkl_host_ops.thread_join(tid);
		if (ret) {
			lkl_test_logf("failed to join thread\n");
			return TEST_FAILURE;
		}
	}

	return TEST_SUCCESS;
}

struct lkl_test_tgid {
	long tgid;
	long parent_pid;
	int new_thread_group_leader_result;
};

static void thread_get_tgid(void *arg)
{
	struct lkl_test_tgid *tgid_arg = (struct lkl_test_tgid *)arg;

	int ret = lkl_sys_new_thread_group_leader();

	tgid_arg->new_thread_group_leader_result = ret;
	if (!ret) {
		tgid_arg->tgid = lkl_sys_getpid();
		tgid_arg->parent_pid = lkl_sys_getppid();
	}
}

static int lkl_test_new_tgid_threads(void)
{
	lkl_thread_t tid;
	long current_pid;
	int count = 65, ret;
	struct lkl_test_tgid test_tgid;

	current_pid = lkl_sys_getpid();
	if (current_pid < 0) {
		lkl_test_logf("failed to get pid\n");
		return TEST_FAILURE;
	}

	while (--count > 0) {
		tid = lkl_host_ops.thread_create(thread_get_tgid, &test_tgid);
		if (!tid) {
			lkl_test_logf("failed to create thread\n");
			return TEST_FAILURE;
		}

		ret = lkl_host_ops.thread_join(tid);
		if (ret) {
			lkl_test_logf("failed to join thread\n");
			return TEST_FAILURE;
		}

		if (test_tgid.new_thread_group_leader_result) {
			lkl_test_logf("failed to set new thread group leader %x\n",
				test_tgid.new_thread_group_leader_result);
			return TEST_FAILURE;
		}

		if (test_tgid.tgid == current_pid) {
			lkl_test_logf("child tgid %lx == current pid %lx\n",
				test_tgid.tgid, current_pid);
			return TEST_FAILURE;
		}

		if (test_tgid.parent_pid != current_pid) {
			lkl_test_logf("ppid of the child %lx != current pid %lx\n",
				test_tgid.parent_pid, current_pid);
			return TEST_FAILURE;
		}
	}

	return TEST_SUCCESS;
}
#endif

static void thread_quit_immediately(void *unused)
{
}

static int lkl_test_join(void)
{
	lkl_thread_t tid = lkl_host_ops.thread_create(thread_quit_immediately, NULL);
	int ret = lkl_host_ops.thread_join(tid);

	if (ret == 0) {
		lkl_test_logf("joined %ld\n", tid);
		return TEST_SUCCESS;
	} else {
		lkl_test_logf("failed joining %ld\n", tid);
		return TEST_FAILURE;
	}
}

static const char *boot_log;

#ifdef LKL_HOST_CONFIG_KASAN_TEST

#define KASAN_CMD_LINE "kunit.filter_glob=kasan* "

static int lkl_test_kasan(void)
{
	char *log = strdup(boot_log);
	char *line = NULL;
	char c, d;

	line = strtok(log, "\n");
	while (line) {
		if (sscanf(line, "[ %*f] ok %*d kasa%c%c", &c, &d) == 1 &&
			   c == 'n') {
			lkl_test_logf("%s", line);
			return TEST_SUCCESS;
		}

		line = strtok(NULL, "\n");
	}

	free(log);

	return TEST_FAILURE;
}
#else
#define KASAN_CMD_LINE
#endif

#ifdef LKL_HOST_CONFIG_MMU

// TODO: for some reason <lkl/linux/mman.h> doesn't contain definition of
// LKL_MAP_FAILED macro. Thus, temporarily define it here.
#ifndef LKL_MAP_FAILED
#define LKL_MAP_FAILED ((void *)-1)
#endif

static int lkl_test_shared_mmap(void)
{
	int fd = lkl_sys_open("/tmp_file_", LKL_O_RDWR | LKL_O_CREAT, LKL_S_IRWXU);

	if (fd < 0)
		return TEST_FAILURE;

	unsigned long mem_size = 15 * 4096;

	if (lkl_sys_ftruncate(fd, mem_size) < 0)
		return TEST_FAILURE;

	int mem_prot = LKL_PROT_WRITE | LKL_PROT_READ;
	// Super important to use LKL_MAP_POPULATE to force populating pages in the
	// mapping as in the LKL context we don't have a way to fault in the pages.
	int mem_flags =  LKL_MAP_SHARED | LKL_MAP_POPULATE;
	void *mem1 = lkl_sys_mmap(0, mem_size, mem_prot, mem_flags, fd, 0);

	if (mem1 == LKL_MAP_FAILED)
		return TEST_FAILURE;

	memset(mem1, 0x17, mem_size);

	void *mem2 = lkl_sys_mmap(0, mem_size, mem_prot, mem_flags, fd, 0);

	if (mem2 == LKL_MAP_FAILED)
		return TEST_FAILURE;

	// This should not happen
	if (mem1 == mem2)
		return TEST_FAILURE;

	if (memcmp(mem1, mem2, mem_size) != 0)
		return TEST_FAILURE;

	if (lkl_sys_munmap((unsigned long)mem1, mem_size) != 0)
		return TEST_FAILURE;

	if (lkl_sys_munmap((unsigned long)mem2, mem_size) != 0)
		return TEST_FAILURE;

	return TEST_SUCCESS;
}

static int lkl_test_private_mmap(void)
{
	unsigned long mem_size = 3 * 4096;
	int mem_prot = LKL_PROT_WRITE | LKL_PROT_READ;
	// Super important to use LKL_MAP_POPULATE to force populating pages in the
	// mapping as in the LKL context we don't have a way to fault in the pages.
	int mem_flags =  LKL_MAP_ANONYMOUS | LKL_MAP_PRIVATE | LKL_MAP_POPULATE;
	void *mem = lkl_sys_mmap(0, mem_size, mem_prot, mem_flags, -1, 0);

	if (mem == LKL_MAP_FAILED)
		return TEST_FAILURE;

	*(unsigned int *)mem = 13;

	if (lkl_sys_munmap((unsigned long)mem, mem_size) < 0)
		return TEST_FAILURE;

	return TEST_SUCCESS;
}
#endif // LKL_HOST_CONFIG_MMU

#ifdef LKL_HOST_CONFIG_LKL_MMU_TEST
static int lkl_test_kunit_mmu(void)
{
	char *log = strdup(boot_log);
	char *line = NULL;
	int n;
	char c, d;

	line = strtok(log, "\n");
	while (line) {
		if (sscanf(line, "[ %*f] ok %d lkl_m%c%c", &n, &c, &d) == 3 &&
			c == 'm' && d == 'u') {
			lkl_test_logf("%s", line);
			return TEST_SUCCESS;
		}

		line = strtok(NULL, "\n");
	}

	free(log);

	return TEST_FAILURE;
}

#define LKL_MMU_TEST_CMD_LINE "kunit.filter_glob=lkl_mmu "
#else
#define LKL_MMU_TEST_CMD_LINE
#endif // LKL_HOST_CONFIG_LKL_MMU_TEST

#define CMD_LINE "mem=32M loglevel=8 " KASAN_CMD_LINE LKL_MMU_TEST_CMD_LINE

static int lkl_test_start_kernel(void)
{
	int ret;

	ret = lkl_start_kernel(CMD_LINE);

	boot_log = lkl_test_get_log();

	return ret == 0 ? TEST_SUCCESS : TEST_FAILURE;
}


LKL_TEST_CALL(stop_kernel, lkl_sys_halt, 0);

struct lkl_test tests[] = {
	LKL_TEST(mutex),
	LKL_TEST(semaphore),
	LKL_TEST(join),
	LKL_TEST(start_kernel),
#ifdef LKL_HOST_CONFIG_KASAN_TEST
	LKL_TEST(kasan),
#endif
	LKL_TEST(getpid),
	LKL_TEST(syscall_latency),
	LKL_TEST(umask),
	LKL_TEST(umask2),
	LKL_TEST(creat),
	LKL_TEST(close),
	LKL_TEST(failopen),
	LKL_TEST(open),
	LKL_TEST(write),
	LKL_TEST(lseek_cur),
	LKL_TEST(lseek_end),
	LKL_TEST(lseek_set),
	LKL_TEST(read),
	LKL_TEST(fstat),
	LKL_TEST(mkdir),
	LKL_TEST(stat),
#ifndef __MINGW32__
	LKL_TEST(nanosleep),
#endif
	LKL_TEST(pipe2),
	LKL_TEST(epoll),
	LKL_TEST(mount_fs_proc),
	LKL_TEST(chdir_proc),
	LKL_TEST(open_cwd),
	LKL_TEST(getdents64),
	LKL_TEST(close_dir_fd),
	LKL_TEST(chdir_root),
	LKL_TEST(umount_fs_proc),
	LKL_TEST(lo_ifup),
	LKL_TEST(syscall_thread),
	/*
	 * Wine has an issue where the FlsCallback is not called when
	 * the thread terminates which makes testing the automatic
	 * syscall threads cleanup impossible under wine.
	 */
#ifndef __MINGW32__
	LKL_TEST(many_syscall_threads),
	LKL_TEST(new_tgid_threads),
#endif
#ifdef LKL_HOST_CONFIG_MMU
	LKL_TEST(shared_mmap),
	LKL_TEST(private_mmap),
#endif
#ifdef LKL_HOST_CONFIG_LKL_MMU_TEST
	LKL_TEST(kunit_mmu),
#endif
	LKL_TEST(stop_kernel),
};

int main(int argc, const char **argv)
{
	int ret;

	lkl_host_ops.print = lkl_test_log;

	if (lkl_init(&lkl_host_ops) < 0) {
		printf("%s\n", lkl_test_get_log());
		return 1;
	}

	ret = lkl_test_run(tests, sizeof(tests)/sizeof(struct lkl_test),
			"boot");

	lkl_cleanup();

	return ret;
}
