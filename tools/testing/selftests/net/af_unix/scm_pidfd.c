// SPDX-License-Identifier: GPL-2.0 OR MIT
#define _GNU_SOURCE
#include <error.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../../pidfd/pidfd.h"
#include "kselftest_harness.h"

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_err(MSG, ...)                                                   \
	fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", __FILE__, __LINE__, \
		clean_errno(), ##__VA_ARGS__)

#ifndef SCM_PIDFD
#define SCM_PIDFD 0x04
#endif

#ifndef SCM_PIDFD_THREAD
#define SCM_PIDFD_THREAD 0x05
#endif

#ifndef SO_PASSPIDFD_THREAD
#define SO_PASSPIDFD_THREAD 86
#endif

#ifndef SO_PEERPIDFD_THREAD
#define SO_PEERPIDFD_THREAD 87
#endif

#define CHILD_EXIT_CODE_OK 123

static void child_die()
{
	exit(1);
}

static int safe_int(const char *numstr, int *converted)
{
	char *err = NULL;
	long sli;

	errno = 0;
	sli = strtol(numstr, &err, 0);
	if (errno == ERANGE && (sli == LONG_MAX || sli == LONG_MIN))
		return -ERANGE;

	if (errno != 0 && sli == 0)
		return -EINVAL;

	if (err == numstr || *err != '\0')
		return -EINVAL;

	if (sli > INT_MAX || sli < INT_MIN)
		return -ERANGE;

	*converted = (int)sli;
	return 0;
}

static int char_left_gc(const char *buffer, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (buffer[i] == ' ' || buffer[i] == '\t')
			continue;

		return i;
	}

	return 0;
}

static int char_right_gc(const char *buffer, size_t len)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (buffer[i] == ' ' || buffer[i] == '\t' ||
		    buffer[i] == '\n' || buffer[i] == '\0')
			continue;

		return i + 1;
	}

	return 0;
}

static char *trim_whitespace_in_place(char *buffer)
{
	buffer += char_left_gc(buffer, strlen(buffer));
	buffer[char_right_gc(buffer, strlen(buffer))] = '\0';
	return buffer;
}

/* borrowed (with all helpers) from pidfd/pidfd_open_test.c */
static pid_t get_pid_from_fdinfo_file(int pidfd, const char *key, size_t keylen)
{
	int ret;
	char path[512];
	FILE *f;
	size_t n = 0;
	pid_t result = -1;
	char *line = NULL;

	snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", pidfd);

	f = fopen(path, "re");
	if (!f)
		return -1;

	while (getline(&line, &n, f) != -1) {
		char *numstr;

		if (strncmp(line, key, keylen))
			continue;

		numstr = trim_whitespace_in_place(line + 4);
		ret = safe_int(numstr, &result);
		if (ret < 0)
			goto out;

		break;
	}

out:
	free(line);
	fclose(f);
	return result;
}

struct cmsg_data {
	struct ucred *ucred;
	int *pidfd;
};

static int parse_cmsg(struct msghdr *msg, struct cmsg_data *res)
{
	struct cmsghdr *cmsg;

	if (msg->msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
		log_err("recvmsg: truncated");
		return 1;
	}

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_PIDFD) {
			if (cmsg->cmsg_len < sizeof(*res->pidfd)) {
				log_err("CMSG parse: SCM_PIDFD wrong len");
				return 1;
			}

			res->pidfd = (void *)CMSG_DATA(cmsg);
		}

		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_CREDENTIALS) {
			if (cmsg->cmsg_len < sizeof(*res->ucred)) {
				log_err("CMSG parse: SCM_CREDENTIALS wrong len");
				return 1;
			}

			res->ucred = (void *)CMSG_DATA(cmsg);
		}
	}

	if (!res->pidfd) {
		log_err("CMSG parse: SCM_PIDFD not found");
		return 1;
	}

	if (!res->ucred) {
		log_err("CMSG parse: SCM_CREDENTIALS not found");
		return 1;
	}

	return 0;
}

static int cmsg_check(int fd)
{
	struct msghdr msg = { 0 };
	struct cmsg_data res;
	struct iovec iov;
	int data = 0;
	char control[CMSG_SPACE(sizeof(struct ucred)) +
		     CMSG_SPACE(sizeof(int))] = { 0 };
	pid_t parent_pid;
	int err;

	iov.iov_base = &data;
	iov.iov_len = sizeof(data);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	err = recvmsg(fd, &msg, 0);
	if (err < 0) {
		log_err("recvmsg");
		return 1;
	}

	if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
		log_err("recvmsg: truncated");
		return 1;
	}

	/* send(pfd, "x", sizeof(char), 0) */
	if (data != 'x') {
		log_err("recvmsg: data corruption");
		return 1;
	}

	if (parse_cmsg(&msg, &res)) {
		log_err("CMSG parse: parse_cmsg() failed");
		return 1;
	}

	/* pidfd from SCM_PIDFD should point to the parent process PID */
	parent_pid =
		get_pid_from_fdinfo_file(*res.pidfd, "Pid:", sizeof("Pid:") - 1);
	if (parent_pid != getppid()) {
		log_err("wrong SCM_PIDFD %d != %d", parent_pid, getppid());
		close(*res.pidfd);
		return 1;
	}

	close(*res.pidfd);
	return 0;
}

static int cmsg_check_dead(int fd, int expected_pid)
{
	int err;
	struct msghdr msg = { 0 };
	struct cmsg_data res;
	struct iovec iov;
	int data = 0;
	char control[CMSG_SPACE(sizeof(struct ucred)) +
		     CMSG_SPACE(sizeof(int))] = { 0 };
	struct pidfd_info info = {
		.mask = PIDFD_INFO_EXIT,
	};

	iov.iov_base = &data;
	iov.iov_len = sizeof(data);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	err = recvmsg(fd, &msg, 0);
	if (err < 0) {
		log_err("recvmsg");
		return 1;
	}

	if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
		log_err("recvmsg: truncated");
		return 1;
	}

	/* send(cfd, "y", sizeof(char), 0) */
	if (data != 'y') {
		log_err("recvmsg: data corruption");
		return 1;
	}

	if (parse_cmsg(&msg, &res)) {
		log_err("CMSG parse: parse_cmsg() failed");
		return 1;
	}

	/*
	 * pidfd from SCM_PIDFD should point to the client_pid.
	 * Let's read exit information and check if it's what
	 * we expect to see.
	 */
	if (ioctl(*res.pidfd, PIDFD_GET_INFO, &info)) {
		log_err("%s: ioctl(PIDFD_GET_INFO) failed", __func__);
		close(*res.pidfd);
		return 1;
	}

	if (!(info.mask & PIDFD_INFO_EXIT)) {
		log_err("%s: No exit information from ioctl(PIDFD_GET_INFO)", __func__);
		close(*res.pidfd);
		return 1;
	}

	err = WIFEXITED(info.exit_code) ? WEXITSTATUS(info.exit_code) : 1;
	if (err != CHILD_EXIT_CODE_OK) {
		log_err("%s: wrong exit_code %d != %d", __func__, err, CHILD_EXIT_CODE_OK);
		close(*res.pidfd);
		return 1;
	}

	close(*res.pidfd);
	return 0;
}

struct sock_addr {
	char sock_name[32];
	struct sockaddr_un listen_addr;
	socklen_t addrlen;
};

FIXTURE(scm_pidfd)
{
	int server;
	pid_t client_pid;
	int startup_pipe[2];
	struct sock_addr server_addr;
	struct sock_addr *client_addr;
};

FIXTURE_VARIANT(scm_pidfd)
{
	int type;
	bool abstract;
};

FIXTURE_VARIANT_ADD(scm_pidfd, stream_pathname)
{
	.type = SOCK_STREAM,
	.abstract = 0,
};

FIXTURE_VARIANT_ADD(scm_pidfd, stream_abstract)
{
	.type = SOCK_STREAM,
	.abstract = 1,
};

FIXTURE_VARIANT_ADD(scm_pidfd, dgram_pathname)
{
	.type = SOCK_DGRAM,
	.abstract = 0,
};

FIXTURE_VARIANT_ADD(scm_pidfd, dgram_abstract)
{
	.type = SOCK_DGRAM,
	.abstract = 1,
};

FIXTURE_SETUP(scm_pidfd)
{
	self->client_addr = mmap(NULL, sizeof(*self->client_addr), PROT_READ | PROT_WRITE,
				 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, self->client_addr);
}

FIXTURE_TEARDOWN(scm_pidfd)
{
	close(self->server);

	kill(self->client_pid, SIGKILL);
	waitpid(self->client_pid, NULL, 0);

	if (!variant->abstract) {
		unlink(self->server_addr.sock_name);
		unlink(self->client_addr->sock_name);
	}
}

static void fill_sockaddr(struct sock_addr *addr, bool abstract)
{
	char *sun_path_buf = (char *)&addr->listen_addr.sun_path;

	addr->listen_addr.sun_family = AF_UNIX;
	addr->addrlen = offsetof(struct sockaddr_un, sun_path);
	snprintf(addr->sock_name, sizeof(addr->sock_name), "scm_pidfd_%d", getpid());
	addr->addrlen += strlen(addr->sock_name);
	if (abstract) {
		*sun_path_buf = '\0';
		addr->addrlen++;
		sun_path_buf++;
	} else {
		unlink(addr->sock_name);
	}
	memcpy(sun_path_buf, addr->sock_name, strlen(addr->sock_name));
}

static int sk_enable_cred_pass(int sk)
{
	int on = 0;

	on = 1;
	if (setsockopt(sk, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on))) {
		log_err("Failed to set SO_PASSCRED");
		return 1;
	}

	if (setsockopt(sk, SOL_SOCKET, SO_PASSPIDFD, &on, sizeof(on))) {
		log_err("Failed to set SO_PASSPIDFD");
		return 1;
	}

	return 0;
}

static void client(FIXTURE_DATA(scm_pidfd) *self,
		   const FIXTURE_VARIANT(scm_pidfd) *variant)
{
	int cfd;
	socklen_t len;
	struct ucred peer_cred;
	int peer_pidfd;
	pid_t peer_pid;

	cfd = socket(AF_UNIX, variant->type, 0);
	if (cfd < 0) {
		log_err("socket");
		child_die();
	}

	if (variant->type == SOCK_DGRAM) {
		fill_sockaddr(self->client_addr, variant->abstract);

		if (bind(cfd, (struct sockaddr *)&self->client_addr->listen_addr, self->client_addr->addrlen)) {
			log_err("bind");
			child_die();
		}
	}

	if (connect(cfd, (struct sockaddr *)&self->server_addr.listen_addr,
		    self->server_addr.addrlen) != 0) {
		log_err("connect");
		child_die();
	}

	if (sk_enable_cred_pass(cfd)) {
		log_err("sk_enable_cred_pass() failed");
		child_die();
	}

	close(self->startup_pipe[1]);

	if (cmsg_check(cfd)) {
		log_err("cmsg_check failed");
		child_die();
	}

	/* send something to the parent so it can receive SCM_PIDFD too and validate it */
	if (send(cfd, "y", sizeof(char), 0) == -1) {
		log_err("Failed to send(cfd, \"y\", sizeof(char), 0)");
		child_die();
	}

	/* skip further for SOCK_DGRAM as it's not applicable */
	if (variant->type == SOCK_DGRAM)
		return;

	len = sizeof(peer_cred);
	if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &peer_cred, &len)) {
		log_err("Failed to get SO_PEERCRED");
		child_die();
	}

	len = sizeof(peer_pidfd);
	if (getsockopt(cfd, SOL_SOCKET, SO_PEERPIDFD, &peer_pidfd, &len)) {
		log_err("Failed to get SO_PEERPIDFD");
		child_die();
	}

	/* pid from SO_PEERCRED should point to the parent process PID */
	if (peer_cred.pid != getppid()) {
		log_err("peer_cred.pid != getppid(): %d != %d", peer_cred.pid, getppid());
		child_die();
	}

	peer_pid = get_pid_from_fdinfo_file(peer_pidfd,
					    "Pid:", sizeof("Pid:") - 1);
	if (peer_pid != peer_cred.pid) {
		log_err("peer_pid != peer_cred.pid: %d != %d", peer_pid, peer_cred.pid);
		child_die();
	}
}

TEST_F(scm_pidfd, test)
{
	int err;
	int pfd;
	int child_status = 0;

	self->server = socket(AF_UNIX, variant->type, 0);
	ASSERT_NE(-1, self->server);

	fill_sockaddr(&self->server_addr, variant->abstract);

	err = bind(self->server, (struct sockaddr *)&self->server_addr.listen_addr, self->server_addr.addrlen);
	ASSERT_EQ(0, err);

	if (variant->type == SOCK_STREAM) {
		err = listen(self->server, 1);
		ASSERT_EQ(0, err);
	}

	err = pipe(self->startup_pipe);
	ASSERT_NE(-1, err);

	self->client_pid = fork();
	ASSERT_NE(-1, self->client_pid);
	if (self->client_pid == 0) {
		close(self->server);
		close(self->startup_pipe[0]);
		client(self, variant);

		/*
		 * It's a bit unusual, but in case of success we return non-zero
		 * exit code (CHILD_EXIT_CODE_OK) and then we expect to read it
		 * from ioctl(PIDFD_GET_INFO) in cmsg_check_dead().
		 */
		exit(CHILD_EXIT_CODE_OK);
	}
	close(self->startup_pipe[1]);

	if (variant->type == SOCK_STREAM) {
		pfd = accept(self->server, NULL, NULL);
		ASSERT_NE(-1, pfd);
	} else {
		pfd = self->server;
	}

	/* wait until the child arrives at checkpoint */
	read(self->startup_pipe[0], &err, sizeof(int));
	close(self->startup_pipe[0]);

	if (variant->type == SOCK_DGRAM) {
		err = sendto(pfd, "x", sizeof(char), 0, (struct sockaddr *)&self->client_addr->listen_addr, self->client_addr->addrlen);
		ASSERT_NE(-1, err);
	} else {
		err = send(pfd, "x", sizeof(char), 0);
		ASSERT_NE(-1, err);
	}

	waitpid(self->client_pid, &child_status, 0);
	/* see comment before exit(CHILD_EXIT_CODE_OK) */
	ASSERT_EQ(CHILD_EXIT_CODE_OK, WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 1);

	err = sk_enable_cred_pass(pfd);
	ASSERT_EQ(0, err);

	err = cmsg_check_dead(pfd, self->client_pid);
	ASSERT_EQ(0, err);

	close(pfd);
}

struct thread_ids {
	pid_t pid;
	pid_t tid;
};

#define MAX_WRITERS 2

/* Used by writers to signal they've written, so we can sequence multiple writers */
static int seq_pipe[2];

static void *send_ids_thread(void *arg)
{
	int fd = *(int *)arg;
	struct thread_ids ids = {
		.pid = getpid(),
		.tid = gettid(),
	};
	char sync;

	if (send(fd, &ids, sizeof(ids), 0) != sizeof(ids))
		return (void *)1;

	/* let the client start the next writer, so the queue order is known */
	if (write(seq_pipe[1], "1", 1) != 1)
		return (void *)1;

	/* stay alive until the receiver has looked at our pidfd */
	if (read(fd, &sync, 1) != 1)
		return (void *)1;

	return NULL;
}

static void *send_ids_creds_thread(void *arg)
{
	int fd = *(int *)arg;
	struct thread_ids ids = {
		.pid = getpid(),
		.tid = gettid(),
	};
	struct ucred ucred = {
		.pid = getpid(),
		.uid = getuid(),
		.gid = getgid(),
	};
	char control[CMSG_SPACE(sizeof(ucred))] = { 0 };
	struct iovec iov;
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	char sync;

	iov.iov_base = &ids;
	iov.iov_len = sizeof(ids);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_CREDENTIALS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(ucred));
	memcpy(CMSG_DATA(cmsg), &ucred, sizeof(ucred));

	if (sendmsg(fd, &msg, 0) != sizeof(ids))
		return (void *)1;

	/* signal we've written our info */
	if (write(seq_pipe[1], "1", 1) != 1)
		return (void *)1;

	/* stay alive until the receiver has looked at our pidfd */
	if (read(fd, &sync, 1) != 1)
		return (void *)1;

	return NULL;
}

/*
 * Runs @nwriters copies of @sender, each in its own non-leader thread, one
 * at a time so the queue order is known, then tells the parent on @ackfd
 * that everything is queued.
 */
static void thread_client(int fd, int syncfd, int ackfd,
			  void *(*sender)(void *), int nwriters)
{
	pthread_t writers[MAX_WRITERS];
	void *ret;
	char sync;
	int i;

	/* wait until the receiver enabled the options it wants */
	if (read(syncfd, &sync, 1) != 1)
		child_die();

	for (i = 0; i < nwriters; i++) {
		if (pthread_create(&writers[i], NULL, sender, &fd))
			child_die();

		if (read(seq_pipe[0], &sync, 1) != 1)
			child_die();
	}

	if (write(ackfd, "1", 1) != 1)
		child_die();

	for (i = 0; i < nwriters; i++)
		if (pthread_join(writers[i], &ret) || ret)
			child_die();

	exit(0);
}

struct pidfd_msg {
	ssize_t len;
	struct thread_ids ids;
	struct pidfd_info tgid_info;
	struct pidfd_info thread_info;
	int tgid_flags;
	int thread_flags;
	bool have_tgid;
	bool have_thread;
};

static int get_pidfd_info(int pidfd, struct pidfd_info *info)
{
	info->mask = PIDFD_INFO_PID;
	if (ioctl(pidfd, PIDFD_GET_INFO, info)) {
		log_err("ioctl(PIDFD_GET_INFO)");
		return -1;
	}

	return 0;
}

/* One recvmsg(), reporting how many bytes came back and what the pidfd
 * cmsgs that came with them say.
 */
static int read_pidfd_msg(int fd, struct pidfd_msg *out)
{
	char control[CMSG_SPACE(sizeof(int)) * 2] = { 0 };
	int tgid_pidfd = -1, thread_pidfd = -1;
	struct thread_ids buf[2] = { 0 };
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	struct iovec iov;

	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	out->len = recvmsg(fd, &msg, 0);
	if (out->len < 0) {
		log_err("recvmsg");
		return -1;
	}

	if (msg.msg_flags & MSG_CTRUNC) {
		log_err("recvmsg: control truncated");
		return -1;
	}

	out->ids = buf[0];

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_SOCKET)
			continue;

		if (cmsg->cmsg_type == SCM_PIDFD)
			memcpy(&tgid_pidfd, CMSG_DATA(cmsg), sizeof(tgid_pidfd));
		else if (cmsg->cmsg_type == SCM_PIDFD_THREAD)
			memcpy(&thread_pidfd, CMSG_DATA(cmsg), sizeof(thread_pidfd));
	}

	out->have_tgid = tgid_pidfd >= 0;
	out->have_thread = thread_pidfd >= 0;

	if (out->have_tgid) {
		if (get_pidfd_info(tgid_pidfd, &out->tgid_info))
			return -1;
		out->tgid_flags = fcntl(tgid_pidfd, F_GETFL);
		close(tgid_pidfd);
	}

	if (out->have_thread) {
		if (get_pidfd_info(thread_pidfd, &out->thread_info))
			return -1;
		out->thread_flags = fcntl(thread_pidfd, F_GETFL);
		close(thread_pidfd);
	}

	return 0;
}

/*
 * Runs @nwriters threads of one child process against a SOCK_STREAM pair,
 * all of them queued before the parent reads. SO_PASSPIDFD is set when
 * @want_tgid is given, SO_PASSPIDFD_THREAD when @want_thread is. @nread
 * reads are returned in @out.
 */
static int pidfd_flow(void *(*sender)(void *), int nwriters, bool want_tgid,
		      bool want_thread, struct pidfd_msg *out, int nread)
{
	int child_status = 0;
	int syncpipe[2];
	int ackpipe[2];
	int sk[2];
	int on = 1;
	char sync;
	pid_t child;
	int i;

	if (nwriters > MAX_WRITERS)
		return -1;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sk))
		return -1;
	if (pipe(syncpipe) || pipe(ackpipe) || pipe(seq_pipe))
		return -1;

	child = fork();
	if (child < 0)
		return -1;

	if (child == 0) {
		close(sk[0]);
		close(syncpipe[1]);
		close(ackpipe[0]);
		thread_client(sk[1], syncpipe[0], ackpipe[1], sender, nwriters);
	}
	close(sk[1]);
	close(syncpipe[0]);
	close(ackpipe[1]);
	close(seq_pipe[0]);
	close(seq_pipe[1]);

	if (want_tgid &&
	    setsockopt(sk[0], SOL_SOCKET, SO_PASSPIDFD, &on, sizeof(on))) {
		log_err("Failed to set SO_PASSPIDFD");
		return -1;
	}

	if (want_thread &&
	    setsockopt(sk[0], SOL_SOCKET, SO_PASSPIDFD_THREAD, &on, sizeof(on))) {
		log_err("Failed to set SO_PASSPIDFD_THREAD");
		return -1;
	}

	/* let the child know the options are set, it can write now */
	if (write(syncpipe[1], "1", 1) != 1)
		return -1;
	close(syncpipe[1]);

	/* wait until every writer has queued its message */
	if (read(ackpipe[0], &sync, 1) != 1)
		return -1;
	close(ackpipe[0]);

	for (i = 0; i < nread; i++)
		if (read_pidfd_msg(sk[0], &out[i]))
			return -1;

	/* release the writers */
	for (i = 0; i < nwriters; i++)
		if (write(sk[0], "x", 1) != 1)
			return -1;
	close(sk[0]);

	waitpid(child, &child_status, 0);
	if (!WIFEXITED(child_status) || WEXITSTATUS(child_status))
		return -1;

	return 0;
}

static int sockopt_set(int fd, int optname, int val)
{
	return setsockopt(fd, SOL_SOCKET, optname, &val, sizeof(val));
}

static int sockopt_get(int fd, int optname)
{
	socklen_t len = sizeof(int);
	int val = -1;

	if (getsockopt(fd, SOL_SOCKET, optname, &val, &len))
		return -1;

	return val;
}

TEST(scm_pidfd_setsockopt_values)
{
	int sk[2];

	ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sk));

	/* Verify that the options are truly set independently */
	ASSERT_EQ(0, sockopt_set(sk[0], SO_PASSPIDFD_THREAD, 1));
	ASSERT_EQ(1, sockopt_get(sk[0], SO_PASSPIDFD_THREAD));
	ASSERT_EQ(0, sockopt_get(sk[0], SO_PASSPIDFD));

	ASSERT_EQ(0, sockopt_set(sk[0], SO_PASSPIDFD, 1));
	ASSERT_EQ(1, sockopt_get(sk[0], SO_PASSPIDFD));
	ASSERT_EQ(1, sockopt_get(sk[0], SO_PASSPIDFD_THREAD));

	/* Verify that the options are truly cleared independently */
	ASSERT_EQ(0, sockopt_set(sk[0], SO_PASSPIDFD, 0));
	ASSERT_EQ(0, sockopt_get(sk[0], SO_PASSPIDFD));
	ASSERT_EQ(1, sockopt_get(sk[0], SO_PASSPIDFD_THREAD));

	ASSERT_EQ(0, sockopt_set(sk[0], SO_PASSPIDFD_THREAD, 0));
	ASSERT_EQ(0, sockopt_get(sk[0], SO_PASSPIDFD_THREAD));
	ASSERT_EQ(0, sockopt_get(sk[0], SO_PASSPIDFD));

	close(sk[0]);
	close(sk[1]);
}

/* A receiver that only asked about the process cannot tell the two
 * threads apart, so their writes are glued into one read.
 */
TEST(scm_pidfd_stream_glues_threads)
{
	struct pidfd_msg msg[1] = { 0 };

	ASSERT_EQ(0, pidfd_flow(send_ids_thread, 2, true, false, msg, 1));
	EXPECT_EQ(sizeof(struct thread_ids) * 2, msg[0].len);

	EXPECT_TRUE(msg[0].have_tgid);
	EXPECT_FALSE(msg[0].have_thread);
	EXPECT_EQ(msg[0].ids.pid, msg[0].tgid_info.pid);
	EXPECT_EQ(msg[0].ids.pid, msg[0].tgid_info.tgid);
}

/* A receiver asking for both thread and process pidfd should get a unique
 * msg for each thread writing
 */
TEST(scm_pidfd_thread_stream_splits_on_threads)
{
	struct pidfd_msg msg[2] = { 0 };
	int i;

	ASSERT_EQ(0, pidfd_flow(send_ids_thread, 2, true, true, msg, 2));

	for (i = 0; i < 2; i++) {
		EXPECT_EQ(sizeof(struct thread_ids), msg[i].len);
		EXPECT_TRUE(msg[i].have_tgid);
		EXPECT_TRUE(msg[i].have_thread);

		/* Make sure the info makes sense for the pidfd type */
		EXPECT_EQ(msg[i].ids.pid, msg[i].tgid_info.pid);
		EXPECT_EQ(msg[i].ids.pid, msg[i].tgid_info.tgid);
		EXPECT_EQ(msg[i].ids.tid, msg[i].thread_info.pid);
		EXPECT_EQ(msg[i].ids.pid, msg[i].thread_info.tgid);
		EXPECT_NE(msg[i].ids.pid, msg[i].ids.tid);
	}

	/* Make sure we really got unique threads per message */
	EXPECT_NE(msg[0].ids.tid, msg[1].ids.tid);
	EXPECT_EQ(msg[0].ids.pid, msg[1].ids.pid);
	EXPECT_EQ(msg[0].tgid_info.pid, msg[1].tgid_info.pid);
	EXPECT_NE(msg[0].thread_info.pid, msg[1].thread_info.pid);
}

/* Sends from the thread-group leader */
static int leader_flow(bool want_thread, struct pidfd_msg *out)
{
	struct thread_ids ids = {
		.pid = getpid(),
		.tid = gettid(),
	};
	int sk[2];
	int on = 1;
	int ret;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sk))
		return -1;

	if (setsockopt(sk[0], SOL_SOCKET, SO_PASSPIDFD, &on, sizeof(on))) {
		log_err("Failed to set SO_PASSPIDFD");
		return -1;
	}

	if (want_thread &&
	    setsockopt(sk[0], SOL_SOCKET, SO_PASSPIDFD_THREAD, &on, sizeof(on))) {
		log_err("Failed to set SO_PASSPIDFD_THREAD");
		return -1;
	}

	if (send(sk[1], &ids, sizeof(ids), 0) != sizeof(ids)) {
		log_err("send");
		return -1;
	}

	ret = read_pidfd_msg(sk[0], out);

	close(sk[0]);
	close(sk[1]);

	return ret;
}

TEST(scm_pidfd_leader_sender)
{
	struct pidfd_msg msg = { 0 };

	ASSERT_EQ(getpid(), gettid());

	ASSERT_EQ(0, leader_flow(false, &msg));
	ASSERT_TRUE(msg.have_tgid);
	EXPECT_FALSE(msg.have_thread);

	EXPECT_EQ(getpid(), msg.tgid_info.pid);
	EXPECT_EQ(getpid(), msg.tgid_info.tgid);
	EXPECT_FALSE(msg.tgid_flags & O_EXCL);
}

TEST(scm_pidfd_thread_leader_sender)
{
	struct pidfd_msg msg = { 0 };

	ASSERT_EQ(getpid(), gettid());

	ASSERT_EQ(0, leader_flow(true, &msg));
	ASSERT_TRUE(msg.have_tgid);
	ASSERT_TRUE(msg.have_thread);

	EXPECT_EQ(getpid(), msg.tgid_info.pid);
	EXPECT_EQ(getpid(), msg.tgid_info.tgid);
	EXPECT_EQ(getpid(), msg.thread_info.pid);
	EXPECT_EQ(getpid(), msg.thread_info.tgid);

	EXPECT_FALSE(msg.tgid_flags & O_EXCL);
	EXPECT_TRUE(msg.thread_flags & O_EXCL);
}

TEST(scm_pidfd_thread_and_group)
{
	struct pidfd_msg msg = { 0 };

	ASSERT_EQ(0, pidfd_flow(send_ids_thread, 1, true, true, &msg, 1));
	ASSERT_NE(msg.ids.pid, msg.ids.tid);
	ASSERT_TRUE(msg.have_tgid);
	ASSERT_TRUE(msg.have_thread);

	EXPECT_EQ(msg.ids.pid, msg.tgid_info.pid);
	EXPECT_EQ(msg.ids.pid, msg.tgid_info.tgid);

	EXPECT_EQ(msg.ids.tid, msg.thread_info.pid);
	EXPECT_EQ(msg.ids.pid, msg.thread_info.tgid);
}

TEST(scm_pidfd_thread)
{
	struct pidfd_msg msg = { 0 };

	ASSERT_EQ(0, pidfd_flow(send_ids_thread, 1, false, true, &msg, 1));
	ASSERT_NE(msg.ids.pid, msg.ids.tid);
	ASSERT_TRUE(msg.have_thread);
	EXPECT_FALSE(msg.have_tgid);
	EXPECT_EQ(msg.ids.tid, msg.thread_info.pid);
	EXPECT_EQ(msg.ids.pid, msg.thread_info.tgid);
}

TEST(scm_pidfd_thread_group)
{
	struct pidfd_msg msg = { 0 };

	ASSERT_EQ(0, pidfd_flow(send_ids_thread, 1, true, false, &msg, 1));
	ASSERT_NE(msg.ids.pid, msg.ids.tid);
	ASSERT_TRUE(msg.have_tgid);
	EXPECT_FALSE(msg.have_thread);
	EXPECT_EQ(msg.ids.pid, msg.tgid_info.pid);
	EXPECT_EQ(msg.ids.pid, msg.tgid_info.tgid);
}

TEST(scm_pidfd_thread_creds)
{
	struct pidfd_msg msg = { 0 };

	ASSERT_EQ(0, pidfd_flow(send_ids_creds_thread, 1, true, true, &msg, 1));
	ASSERT_NE(msg.ids.pid, msg.ids.tid);
	ASSERT_TRUE(msg.have_tgid);
	ASSERT_TRUE(msg.have_thread);
	EXPECT_EQ(msg.ids.pid, msg.tgid_info.pid);
	EXPECT_EQ(msg.ids.pid, msg.tgid_info.tgid);
	EXPECT_EQ(msg.ids.tid, msg.thread_info.pid);
	EXPECT_EQ(msg.ids.pid, msg.thread_info.tgid);
}

static void *peer_connect_thread(void *arg)
{
	struct sock_addr *sa = arg;
	struct thread_ids ids = {
		.pid = getpid(),
		.tid = gettid(),
	};
	int fd;
	char sync;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return (void *)1;

	if (connect(fd, (struct sockaddr *)&sa->listen_addr, sa->addrlen))
		return (void *)1;

	if (send(fd, &ids, sizeof(ids), 0) != sizeof(ids))
		return (void *)1;

	/* stay alive until the server has looked at our pidfd */
	if (read(fd, &sync, 1) != 1)
		return (void *)1;

	close(fd);
	return NULL;
}

static int peer_pidfd_info(int fd, int optname, struct pidfd_info *info)
{
	int pidfd;
	socklen_t len = sizeof(pidfd);

	if (getsockopt(fd, SOL_SOCKET, optname, &pidfd, &len)) {
		log_err("getsockopt(SO_PEERPIDFD*)");
		return -1;
	}

	info->mask = PIDFD_INFO_PID;
	if (ioctl(pidfd, PIDFD_GET_INFO, info)) {
		log_err("ioctl(PIDFD_GET_INFO)");
		close(pidfd);
		return -1;
	}

	close(pidfd);
	return 0;
}

/* SO_PEERPIDFD_THREAD returns a pidfd for the peer's connecting thread. */
TEST(so_peerpidfd_thread)
{
	struct sock_addr sa;
	struct thread_ids ids;
	struct pidfd_info info;
	pthread_t thread;
	void *tret;
	int server, cfd;

	server = socket(AF_UNIX, SOCK_STREAM, 0);
	ASSERT_LE(0, server);

	fill_sockaddr(&sa, true);
	ASSERT_EQ(0, bind(server, (struct sockaddr *)&sa.listen_addr, sa.addrlen));
	ASSERT_EQ(0, listen(server, 1));

	ASSERT_EQ(0, pthread_create(&thread, NULL, peer_connect_thread, &sa));

	cfd = accept(server, NULL, NULL);
	ASSERT_LE(0, cfd);

	ASSERT_EQ(sizeof(ids), recv(cfd, &ids, sizeof(ids), MSG_WAITALL));
	ASSERT_NE(ids.pid, ids.tid);

	/* SO_PEERPIDFD refers to the peer's thread-group. */
	ASSERT_EQ(0, peer_pidfd_info(cfd, SO_PEERPIDFD, &info));
	EXPECT_EQ(ids.pid, info.pid);
	EXPECT_EQ(ids.pid, info.tgid);

	/* SO_PEERPIDFD_THREAD refers to the connecting thread. */
	ASSERT_EQ(0, peer_pidfd_info(cfd, SO_PEERPIDFD_THREAD, &info));
	EXPECT_EQ(ids.tid, info.pid);
	EXPECT_EQ(ids.pid, info.tgid);

	/* release the connecting thread */
	ASSERT_EQ(1, write(cfd, "x", 1));
	ASSERT_EQ(0, pthread_join(thread, &tret));
	ASSERT_EQ(NULL, tret);

	close(cfd);
	close(server);
}

TEST_HARNESS_MAIN
