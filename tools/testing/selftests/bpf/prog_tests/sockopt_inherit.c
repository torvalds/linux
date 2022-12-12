// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "cgroup_helpers.h"

#define SOL_CUSTOM			0xdeadbeef
#define CUSTOM_INHERIT1			0
#define CUSTOM_INHERIT2			1
#define CUSTOM_LISTENER			2

static int connect_to_server(int server_fd)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		goto out;
	}

	if (connect(fd, (const struct sockaddr *)&addr, len) < 0) {
		log_err("Fail to connect to server");
		goto out;
	}

	return fd;

out:
	close(fd);
	return -1;
}

static int verify_sockopt(int fd, int optname, const char *msg, char expected)
{
	socklen_t optlen = 1;
	char buf = 0;
	int err;

	err = getsockopt(fd, SOL_CUSTOM, optname, &buf, &optlen);
	if (err) {
		log_err("%s: failed to call getsockopt", msg);
		return 1;
	}

	printf("%s %d: got=0x%x ? expected=0x%x\n", msg, optname, buf, expected);

	if (buf != expected) {
		log_err("%s: unexpected getsockopt value %d != %d", msg,
			buf, expected);
		return 1;
	}

	return 0;
}

static pthread_mutex_t server_started_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t server_started = PTHREAD_COND_INITIALIZER;

static void *server_thread(void *arg)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	int fd = *(int *)arg;
	int client_fd;
	int err = 0;

	err = listen(fd, 1);

	pthread_mutex_lock(&server_started_mtx);
	pthread_cond_signal(&server_started);
	pthread_mutex_unlock(&server_started_mtx);

	if (!ASSERT_GE(err, 0, "listed on socket"))
		return NULL;

	err += verify_sockopt(fd, CUSTOM_INHERIT1, "listen", 1);
	err += verify_sockopt(fd, CUSTOM_INHERIT2, "listen", 1);
	err += verify_sockopt(fd, CUSTOM_LISTENER, "listen", 1);

	client_fd = accept(fd, (struct sockaddr *)&addr, &len);
	if (!ASSERT_GE(client_fd, 0, "accept client"))
		return NULL;

	err += verify_sockopt(client_fd, CUSTOM_INHERIT1, "accept", 1);
	err += verify_sockopt(client_fd, CUSTOM_INHERIT2, "accept", 1);
	err += verify_sockopt(client_fd, CUSTOM_LISTENER, "accept", 0);

	close(client_fd);

	return (void *)(long)err;
}

static int start_server(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	char buf;
	int err;
	int fd;
	int i;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create server socket");
		return -1;
	}

	for (i = CUSTOM_INHERIT1; i <= CUSTOM_LISTENER; i++) {
		buf = 0x01;
		err = setsockopt(fd, SOL_CUSTOM, i, &buf, 1);
		if (err) {
			log_err("Failed to call setsockopt(%d)", i);
			close(fd);
			return -1;
		}
	}

	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_err("Failed to bind socket");
		close(fd);
		return -1;
	}

	return fd;
}

static int prog_attach(struct bpf_object *obj, int cgroup_fd, const char *title,
		       const char *prog_name)
{
	enum bpf_attach_type attach_type;
	enum bpf_prog_type prog_type;
	struct bpf_program *prog;
	int err;

	err = libbpf_prog_type_by_name(title, &prog_type, &attach_type);
	if (err) {
		log_err("Failed to deduct types for %s BPF program", prog_name);
		return -1;
	}

	prog = bpf_object__find_program_by_name(obj, prog_name);
	if (!prog) {
		log_err("Failed to find %s BPF program", prog_name);
		return -1;
	}

	err = bpf_prog_attach(bpf_program__fd(prog), cgroup_fd,
			      attach_type, 0);
	if (err) {
		log_err("Failed to attach %s BPF program", prog_name);
		return -1;
	}

	return 0;
}

static void run_test(int cgroup_fd)
{
	int server_fd = -1, client_fd;
	struct bpf_object *obj;
	void *server_err;
	pthread_t tid;
	int err;

	obj = bpf_object__open_file("sockopt_inherit.bpf.o", NULL);
	if (!ASSERT_OK_PTR(obj, "obj_open"))
		return;

	err = bpf_object__load(obj);
	if (!ASSERT_OK(err, "obj_load"))
		goto close_bpf_object;

	err = prog_attach(obj, cgroup_fd, "cgroup/getsockopt", "_getsockopt");
	if (!ASSERT_OK(err, "prog_attach _getsockopt"))
		goto close_bpf_object;

	err = prog_attach(obj, cgroup_fd, "cgroup/setsockopt", "_setsockopt");
	if (!ASSERT_OK(err, "prog_attach _setsockopt"))
		goto close_bpf_object;

	server_fd = start_server();
	if (!ASSERT_GE(server_fd, 0, "start_server"))
		goto close_bpf_object;

	pthread_mutex_lock(&server_started_mtx);
	if (!ASSERT_OK(pthread_create(&tid, NULL, server_thread,
				      (void *)&server_fd), "pthread_create")) {
		pthread_mutex_unlock(&server_started_mtx);
		goto close_server_fd;
	}
	pthread_cond_wait(&server_started, &server_started_mtx);
	pthread_mutex_unlock(&server_started_mtx);

	client_fd = connect_to_server(server_fd);
	if (!ASSERT_GE(client_fd, 0, "connect_to_server"))
		goto close_server_fd;

	ASSERT_OK(verify_sockopt(client_fd, CUSTOM_INHERIT1, "connect", 0), "verify_sockopt1");
	ASSERT_OK(verify_sockopt(client_fd, CUSTOM_INHERIT2, "connect", 0), "verify_sockopt2");
	ASSERT_OK(verify_sockopt(client_fd, CUSTOM_LISTENER, "connect", 0), "verify_sockopt ener");

	pthread_join(tid, &server_err);

	err = (int)(long)server_err;
	ASSERT_OK(err, "pthread_join retval");

	close(client_fd);

close_server_fd:
	close(server_fd);
close_bpf_object:
	bpf_object__close(obj);
}

void test_sockopt_inherit(void)
{
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/sockopt_inherit");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	run_test(cgroup_fd);
	close(cgroup_fd);
}
