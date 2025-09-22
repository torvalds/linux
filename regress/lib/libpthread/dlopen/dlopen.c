/*	$OpenBSD: dlopen.c,v 1.2 2017/09/07 21:35:35 guenther Exp $ */
/*
 * Copyright (c) 2016 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Test that various calls can be interrupted in a non-threaded process,
 * then dlopen() libpthread and do that again in a second thread,
 * and then verify that they're all correctly acting as cancellation points.
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* path of fifo we remove/create/open/remove */
#define FIFO_PATH	"fifo"

/* path of lock file remove/create/lock/remove */
#define LOCK_PATH	"lock"

#define TEST_ACCEPT	0x001
#define TEST_CONNECT	0x002
#define TEST_FCNTL	0x004
#define TEST_FLOCK	0x008
#define TEST_NANOSLEEP	0x010
#define TEST_OPEN_FIFO	0x020
#define TEST_POLL	0x040
#define TEST_SIGSUSPEND	0x080
#define TEST_SEMOP	0x100

#define TEST_ALL	0x1ff

struct test_spec
{
	int flag;
	const char *name;
	void (*init)(void);
	void *(*run)(void *);
	void (*fini)(void);
};


/*
 * Functions looked up in libpthread
 */
int	(*p_attr_init)(pthread_attr_t *);
int	(*p_attr_setdetachstate)(pthread_attr_t *, int);
int	(*p_cancel)(pthread_t);
int	(*p_cond_destroy)(pthread_cond_t *);
int	(*p_cond_timedwait)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int	(*p_create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int	(*p_join)(pthread_t, void **);
int	(*p_mutex_destroy)(pthread_mutex_t *);
int	(*p_mutex_lock)(pthread_mutex_t *);
int	(*p_mutex_unlock)(pthread_mutex_t *);
pthread_t (*p_self)(void);

struct funcs
{
	const char *name;
	void *callback;
} functions[] =
{
#define FUNC(f)		{ "pthread_"#f, &p_##f }
	FUNC(attr_init),
	FUNC(attr_setdetachstate),
	FUNC(cancel),
	FUNC(cond_destroy),
	FUNC(cond_timedwait),
	FUNC(create),
	FUNC(join),
	FUNC(mutex_destroy),
	FUNC(mutex_lock),
	FUNC(mutex_unlock),
	FUNC(self),
	{ NULL, NULL }
#undef FUNC
};

/*
 * Shared cleanup
 */
void
finish(const char *msg, const int *retval, const struct timespec *tsp)
{
	struct timespec after;
	const char *fill = "\t\t\t";

	clock_gettime(CLOCK_REALTIME, &after);
	after.tv_sec -= tsp->tv_sec;
	after.tv_nsec -= tsp->tv_nsec;
	if (after.tv_nsec < 0) {
		after.tv_sec--;
		after.tv_nsec += 1000000000L;
	}                                                       

	fill += (strlen(msg) - 1) / 8;
	if (retval[0] >= 0)
		printf("%s: fail%s\ttime = %ld.%09lu\nr = %d\n",
		    msg, fill, (long)after.tv_sec, after.tv_nsec, retval[0]);
	else if (retval[1] != EINTR)
		printf("%s: fail%s\ttime = %ld.%09lu\nr = %d\terrno = %d: %s\n",
		    msg, fill, (long)after.tv_sec, after.tv_nsec,
		    retval[0], retval[1], strerror(retval[1]));
	else
		printf("%s: pass%s\ttime = %ld.%09lu\n",
		    msg, fill, (long)after.tv_sec, after.tv_nsec);
}

/* noop signal handler */
void
sigusr1(int sig)
{
}

/*
 * Interrupt via alarm()
 */
void
sigalrm(int sig)
{
	write(1, "* ", 2);
}

void
set_sigalrm(int restart)
{
	struct sigaction sa;

	sa.sa_handler = &sigalrm;
	sa.sa_flags = restart ? SA_RESTART : 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
}

void
run_sig(const struct test_spec *test)
{
	struct timespec before;
	int retval[2];

	if (test->init != NULL)
		test->init();
	if (clock_gettime(CLOCK_REALTIME, &before))
		err(1, "clock_gettime");
	alarm(1);
	test->run(retval);
	finish(test->name, retval, &before);
	if (test->fini != NULL)
		test->fini();
}


/*
 * Interrupt via cancellation
 */


void
run_cancel(const struct test_spec *test)
{
	struct timespec before, target_time;
	pthread_t tester;
	pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t c = PTHREAD_COND_INITIALIZER;
	int retval[2];
	int r;

	if (test->init != NULL)
		test->init();

	if ((r = p_mutex_lock(&m)))
		errc(1, r, "pthread_mutex_lock");

	if (clock_gettime(CLOCK_REALTIME, &before))
		err(1, "clock_gettime");

	target_time.tv_sec = before.tv_sec + 1;
	target_time.tv_nsec = before.tv_nsec;

	retval[0] = -2;
	if ((r = p_create(&tester, NULL, test->run, retval)))
		errc(1, r, "pthread_create");

	/* overkill: could have done it with pthread_mutex_timedlock */
	do
		r = p_cond_timedwait(&c, &m, &target_time);
	while (r == 0);
	if (r != ETIMEDOUT)
		errc(1, r, "pthread_cond_timedwait");
	write(1, "* ", 2);
	if (retval[0] == -2) {
		retval[0] = -1;
		retval[1] = EINTR;
	}
	if ((r = p_cancel(tester)))
		errc(1, r, "pthread_cancel");
	if ((r = p_mutex_unlock(&m)))
		errc(1, r, "pthread_mutex_unlock");
	if ((r = p_mutex_destroy(&m)))
		errc(1, r, "pthread_mutex_destroy");
	if ((r = p_cond_destroy(&c)))
		errc(1, r, "pthread_cond_destroy");

	finish(test->name, retval, &before);
	if (test->fini != NULL)
		test->fini();
}

void (*run)(const struct test_spec *_test) = run_sig;


/*
 * The operations that are exercised in the tests
 */

/*
 * POLL
 */
void *
poll_run(void *arg)
{
	struct pollfd pfd;
	int *retval = arg;

	pfd.fd = 0;
	pfd.events = POLLIN;
	retval[0] = poll(&pfd, 1, 3 * 1000);
	retval[1] = errno;
	return NULL;
}

/*
 * NANOSLEEP
 */
void *
nanosleep_run(void *arg)
{
	struct timespec ts;
	int *retval = arg;

	ts.tv_sec = 2;
	ts.tv_nsec = 0;
	retval[0] = nanosleep(&ts, &ts);
	retval[1] = errno;
	return NULL;
}

/*
 * FCNTL
 */
struct flock fcntl_fl = {
	.l_start = 0,
	.l_len = 0,
	.l_type = F_WRLCK,
	.l_whence = SEEK_SET,
};
static int fcntl_fd = -1;
static pid_t fcntl_pid = 0;
void
fcntl_init(void)
{
	int fds[2];
	char buf[1];

	if (unlink(LOCK_PATH) && errno != ENOENT)
		err(1, "unlink %s", LOCK_PATH);
	if (pipe(fds))
		err(1, "pipe");
	fcntl_fd = open(LOCK_PATH, O_RDWR | O_CREAT, 0666);
	fcntl_pid = fork();
	if (fcntl_pid == 0) {
		fcntl(fcntl_fd, F_SETLKW, &fcntl_fl);
		close(fds[0]);
		close(fds[1]);
		sleep(1000);
		_exit(0);
	}
	close(fds[1]);
	read(fds[0], buf, 1);
	close(fds[0]);
}
void *
fcntl_run(void *arg)
{
	int *retval = arg;

	retval[0] = fcntl(fcntl_fd, F_SETLKW, &fcntl_fl);
	retval[1] = errno;
	return NULL;
}
void
fcntl_fini(void)
{
	if (fcntl_fd >= 0) {
		close(fcntl_fd);
		fcntl_fd = -1;
	}
	if (fcntl_pid > 0) {
		kill(fcntl_pid, SIGINT);
		waitpid(fcntl_pid, NULL, 0);
		fcntl_pid = 0;
	}
	if (unlink(LOCK_PATH))
		err(1, "unlink %s", LOCK_PATH);
}

/*
 * FLOCK
 */
static int flock_fd = -1;
static pid_t flock_pid = 0;
void
flock_init(void)
{
	int fds[2];
	char buf[1];

	if (unlink(LOCK_PATH) && errno != ENOENT)
		err(1, "unlink %s", LOCK_PATH);
	if (pipe(fds))
		err(1, "pipe");
	flock_pid = fork();
	flock_fd = open(LOCK_PATH, O_RDWR | O_CREAT, 0666);
	if (flock_pid == 0) {
		flock(flock_fd, LOCK_EX);
		close(fds[0]);
		close(fds[1]);
		sleep(1000);
		_exit(0);
	}
	close(fds[1]);
	read(fds[0], buf, 1);
	close(fds[0]);
}
void *
flock_run(void *arg)
{
	int *retval = arg;

	retval[0] = flock(flock_fd, LOCK_EX);
	retval[1] = errno;
	return NULL;
}
void
flock_fini(void)
{
	if (flock_fd >= 0) {
		close(flock_fd);
		flock_fd = -1;
	}
	if (flock_pid > 0) {
		kill(flock_pid, SIGINT);
		waitpid(flock_pid, NULL, 0);
		flock_pid = 0;
	}
	if (unlink(LOCK_PATH) && errno != ENOENT)
		err(1, "unlink %s", LOCK_PATH);
}

/*
 * SIGSUSPEND
 */
void *
sigsuspend_run(void *arg)
{
	sigset_t set;
	int *retval = arg;

	sigemptyset(&set);
	retval[0] = sigsuspend(&set);
	retval[1] = errno;
	return NULL;
}

/*
 * CONNECT
 */
static int connect_fd = -1;
void
connect_init(void)
{
	int on = 1;

	connect_fd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(connect_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
	setsockopt(connect_fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
}
void *
connect_run(void *arg)
{
	struct sockaddr_in sin;
	int *retval = arg;

	sin.sin_family = AF_INET;
	inet_pton(AF_INET, "223.255.255.255", &sin.sin_addr);
	sin.sin_port = 25;
	retval[0] = connect(connect_fd, (struct sockaddr *)&sin, sizeof(sin));
	retval[1] = errno;
	return NULL;
}
void
connect_fini(void)
{
	if (connect_fd >= 0) {
		close(connect_fd);
		connect_fd = -1;
	}
}

/*
 * ACCEPT
 */
static int accept_fd = -1;
void
accept_init(void)
{
	accept_fd = socket(AF_INET, SOCK_STREAM, 0);
	listen(accept_fd, 2);
}
void *
accept_run(void *arg)
{
	struct sockaddr_in sin;
	socklen_t sl;
	int *retval = arg;

	sl = sizeof(sin);
	retval[0] = accept(accept_fd, (struct sockaddr *)&sin, &sl);
	retval[1] = errno;
	return NULL;
}
void
accept_fini(void)
{
	if (accept_fd >= 0) {
		close(accept_fd);
		accept_fd = -1;
	}
}

/*
 * OPEN FIFO
 */
void
open_fifo_init(void)
{
	/* let's get a fresh fifo */
	if (unlink(FIFO_PATH) && errno != ENOENT)
		err(1, "unlink %s", FIFO_PATH);
	if (mkfifo(FIFO_PATH, 0600))
		err(1, "mkfifo %s", FIFO_PATH);
}
void *
open_fifo_run(void *arg)
{
	int *retval = arg;

	retval[0] = open(FIFO_PATH, O_RDONLY);
	retval[1] = errno;
	return NULL;
}
void
open_fifo_fini(void)
{
	if (unlink(FIFO_PATH) && errno != ENOENT)
		err(1, "unlink %s", FIFO_PATH);
}

/*
 * SEMOP
 */
static int semid = -1;
void
semop_init(void)
{
	union {
		int val;
		struct semid_ds *buf;
		unsigned short *array;
	} semarg;
	unsigned short val;

	semid = semget(IPC_PRIVATE, 1, 0600);
	semarg.array = &val;
	val = 0;
	semctl(semid, 0, SETALL, semarg);
}
void *
semop_run(void *arg)
{
	struct sembuf op;
	int *retval = arg;

	op.sem_num = 0;
	op.sem_op = -1;
	op.sem_flg = 0;
	retval[0] = semop(semid, &op, 1);
	retval[1] = errno;
	return NULL;
}
void
semop_fini(void)
{
	if (semid >= 0) {
		semctl(semid, 0, IPC_RMID, NULL);
		semid = -1;
	}
}

#define TESTSPEC_FULL(flag, name, prefix)				\
	{ flag, name, prefix##_init, prefix##_run, prefix##_fini }
#define TESTSPEC(flag, name, prefix)					\
	{ flag, name, NULL, prefix##_run, NULL }
struct test_spec test_specs[] = {
	TESTSPEC_FULL(TEST_ACCEPT,	"accept",		accept),
	TESTSPEC_FULL(TEST_CONNECT,	"connect",		connect),
	TESTSPEC_FULL(TEST_FCNTL,	"fcntl(F_SETLKW)",	fcntl),
	TESTSPEC_FULL(TEST_SEMOP,	"semop",		semop),
	TESTSPEC_FULL(TEST_FLOCK,	"flock",		flock),
	TESTSPEC_FULL(TEST_OPEN_FIFO,	"open_fifo",		open_fifo),

	TESTSPEC(TEST_NANOSLEEP,	"nanosleep",		nanosleep),
	TESTSPEC(TEST_POLL,		"poll",			poll),
	TESTSPEC(TEST_SIGSUSPEND,	"sigsuspend",		sigsuspend),
	{ 0 }
};


void *
run_tests(void *arg)
{
	int tests = *(int *)arg;
	int flag;
	struct test_spec *test;
	sigset_t mask;

	/* make sure SIGALRM is unblocked for the tests */
	sigemptyset(&mask);
	sigaddset(&mask, SIGALRM);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);

	while (tests > 0) {
		flag = tests & ~(tests >> 1);
		tests &= ~flag;
		for (test = test_specs; test->flag; test++)
			if (test->flag == flag) {
				run(test);
				break;
			}
	}

	return arg;
}

int
main(int argc, char **argv)
{
	int ch, tests;
	sigset_t mask;
	int r;
	void *handle;
	struct funcs *f;
	pthread_t t;
	void *ret;

	set_sigalrm(0);

	tests = 0;
	while ((ch = getopt(argc, argv, "AacFfinoprSs")) != -1)
		switch (ch) {
		  case 'A':
			tests |= TEST_ALL;
			break;
		  case 'a':
			tests |= TEST_ACCEPT;
			break;
		  case 'c':
			tests |= TEST_CONNECT;
			break;
		  case 'F':
			tests |= TEST_FCNTL;
			break;
		  case 'f':
			tests |= TEST_FLOCK;
			break;
		  case 'i':
			set_sigalrm(0);
			break;
		  case 'n':
			tests |= TEST_NANOSLEEP;
			break;
		  case 'o':
			tests |= TEST_OPEN_FIFO;
			break;
		  case 'p':
			tests |= TEST_POLL;
			break;
		  case 'r':
			set_sigalrm(1);
			break;
		  case 's':
			tests |= TEST_SIGSUSPEND;
			break;
		  case 'S':
			tests |= TEST_SEMOP;
			break;
		}
	if (tests == 0)
		tests = TEST_ALL;

	/* make sure SIGTERM is unblocked */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);

	/*
	 * Run them in the original thread
	 */
	printf("single threaded\n");
	run_tests(&tests);


	/*
	 * Open libpthread, create a thread and run them in *that*
	 */
	if ((handle = dlopen("libpthread.so", RTLD_LAZY)) == NULL)
		errx(1, "dlopen: %s", dlerror());

	/* look up all the functions.  The cast here isn't strictly portable */
	for (f = functions; f->name != NULL; f++) {
		if ((*(void **)f->callback = dlsym(handle, f->name)) == NULL &&
		    (*(void **)f->callback = dlsym(RTLD_DEFAULT, f->name)) == NULL)
			errx(1, "dlsym %s: %s", f->name, dlerror());
	}

	/* block SIGALRM in the original thread */
	sigemptyset(&mask);
	sigaddset(&mask, SIGALRM);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	printf("in thread after dlopen(pthread)\n");
	if ((r = p_create(&t, NULL, run_tests, &tests)))
		errc(1, r, "pthread_create");
	if ((r = p_join(t, &ret)))
		errc(1, r, "pthread_join");
	if (ret != &tests)
		errx(1, "bad return by thread: %p != %p", ret, (void *)&tests);


	/*
	 * Run the tests again, this time using cancellation
	 */
	printf("using cancellation\n");
	run = run_cancel;
	run_tests(&tests);

	if (dlclose(handle))
		errx(1, "dlclose: %s", dlerror());

	return 0;
}
