// SPDX-License-Identifier: GPL-2.0
#include <internal/lib.h>
#include <subcmd/parse-options.h>
#include <api/fd/array.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "config.h"
#include "util.h"

struct daemon {
	const char		*config;
	char			*config_real;
	const char		*base_user;
	char			*base;
	FILE			*out;
	char			 perf[PATH_MAX];
};

static struct daemon __daemon = { };

static const char * const daemon_usage[] = {
	"perf daemon start [<options>]",
	"perf daemon [<options>]",
	NULL
};

static bool done;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

static int client_config(const char *var, const char *value, void *cb)
{
	struct daemon *daemon = cb;

	if (!strcmp(var, "daemon.base") && !daemon->base_user) {
		daemon->base = strdup(value);
		if (!daemon->base)
			return -ENOMEM;
	}

	return 0;
}

static int check_base(struct daemon *daemon)
{
	struct stat st;

	if (!daemon->base) {
		pr_err("failed: base not defined\n");
		return -EINVAL;
	}

	if (stat(daemon->base, &st)) {
		switch (errno) {
		case EACCES:
			pr_err("failed: permission denied for '%s' base\n",
			       daemon->base);
			return -EACCES;
		case ENOENT:
			pr_err("failed: base '%s' does not exists\n",
			       daemon->base);
			return -EACCES;
		default:
			pr_err("failed: can't access base '%s': %s\n",
			       daemon->base, strerror(errno));
			return -errno;
		}
	}

	if ((st.st_mode & S_IFMT) != S_IFDIR) {
		pr_err("failed: base '%s' is not directory\n",
		       daemon->base);
		return -EINVAL;
	}

	return 0;
}

static int setup_client_config(struct daemon *daemon)
{
	struct perf_config_set *set = perf_config_set__load_file(daemon->config_real);
	int err = -ENOMEM;

	if (set) {
		err = perf_config_set(set, client_config, daemon);
		perf_config_set__delete(set);
	}

	return err ?: check_base(daemon);
}

static int setup_server_socket(struct daemon *daemon)
{
	struct sockaddr_un addr;
	char path[PATH_MAX];
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (fd < 0) {
		fprintf(stderr, "socket: %s\n", strerror(errno));
		return -1;
	}

	if (fcntl(fd, F_SETFD, FD_CLOEXEC)) {
		perror("failed: fcntl FD_CLOEXEC");
		close(fd);
		return -1;
	}

	scnprintf(path, sizeof(path), "%s/control", daemon->base);

	if (strlen(path) + 1 >= sizeof(addr.sun_path)) {
		pr_err("failed: control path too long '%s'\n", path);
		close(fd);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;

	strlcpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	unlink(path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		perror("failed: bind");
		close(fd);
		return -1;
	}

	if (listen(fd, 1) == -1) {
		perror("failed: listen");
		close(fd);
		return -1;
	}

	return fd;
}

union cmd {
	int cmd;
};

static int handle_server_socket(struct daemon *daemon __maybe_unused, int sock_fd)
{
	int ret = -1, fd;
	FILE *out = NULL;
	union cmd cmd;

	fd = accept(sock_fd, NULL, NULL);
	if (fd < 0) {
		perror("failed: accept");
		return -1;
	}

	if (sizeof(cmd) != readn(fd, &cmd, sizeof(cmd))) {
		perror("failed: read");
		goto out;
	}

	out = fdopen(fd, "w");
	if (!out) {
		perror("failed: fdopen");
		goto out;
	}

	switch (cmd.cmd) {
	default:
		break;
	}

	fclose(out);
out:
	/* If out is defined, then fd is closed via fclose. */
	if (!out)
		close(fd);
	return ret;
}

static int setup_client_socket(struct daemon *daemon)
{
	struct sockaddr_un addr;
	char path[PATH_MAX];
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (fd == -1) {
		perror("failed: socket");
		return -1;
	}

	scnprintf(path, sizeof(path), "%s/control", daemon->base);

	if (strlen(path) + 1 >= sizeof(addr.sun_path)) {
		pr_err("failed: control path too long '%s'\n", path);
		close(fd);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		perror("failed: connect");
		close(fd);
		return -1;
	}

	return fd;
}

static void daemon__exit(struct daemon *daemon)
{
	free(daemon->config_real);
	free(daemon->base);
}

static int setup_config(struct daemon *daemon)
{
	if (daemon->base_user) {
		daemon->base = strdup(daemon->base_user);
		if (!daemon->base)
			return -ENOMEM;
	}

	if (daemon->config) {
		char *real = realpath(daemon->config, NULL);

		if (!real) {
			perror("failed: realpath");
			return -1;
		}
		daemon->config_real = real;
		return 0;
	}

	if (perf_config_system() && !access(perf_etc_perfconfig(), R_OK))
		daemon->config_real = strdup(perf_etc_perfconfig());
	else if (perf_config_global() && perf_home_perfconfig())
		daemon->config_real = strdup(perf_home_perfconfig());

	return daemon->config_real ? 0 : -1;
}

static int __cmd_start(struct daemon *daemon, struct option parent_options[],
		       int argc, const char **argv)
{
	struct option start_options[] = {
		OPT_PARENT(parent_options),
		OPT_END()
	};
	int sock_fd = -1;
	int sock_pos;
	struct fdarray fda;
	int err = 0;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	debug_set_file(daemon->out);
	debug_set_display_time(true);

	pr_info("daemon started (pid %d)\n", getpid());

	fdarray__init(&fda, 1);

	sock_fd = setup_server_socket(daemon);
	if (sock_fd < 0)
		goto out;

	sock_pos = fdarray__add(&fda, sock_fd, POLLIN|POLLERR|POLLHUP, 0);
	if (sock_pos < 0)
		goto out;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	while (!done && !err) {
		if (fdarray__poll(&fda, -1)) {
			if (fda.entries[sock_pos].revents & POLLIN)
				err = handle_server_socket(daemon, sock_fd);
		}
	}

out:
	fdarray__exit(&fda);

	daemon__exit(daemon);

	if (sock_fd != -1)
		close(sock_fd);

	pr_info("daemon exited\n");
	fclose(daemon->out);
	return err;
}

__maybe_unused
static int send_cmd(struct daemon *daemon, union cmd *cmd)
{
	int ret = -1, fd;
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;
	FILE *in = NULL;

	if (setup_client_config(daemon))
		return -1;

	fd = setup_client_socket(daemon);
	if (fd < 0)
		return -1;

	if (sizeof(*cmd) != writen(fd, cmd, sizeof(*cmd))) {
		perror("failed: write");
		goto out;
	}

	in = fdopen(fd, "r");
	if (!in) {
		perror("failed: fdopen");
		goto out;
	}

	while ((nread = getline(&line, &len, in)) != -1) {
		if (fwrite(line, nread, 1, stdout) != 1)
			goto out_fclose;
		fflush(stdout);
	}

	ret = 0;
out_fclose:
	fclose(in);
	free(line);
out:
	/* If in is defined, then fd is closed via fclose. */
	if (!in)
		close(fd);
	return ret;
}

int cmd_daemon(int argc, const char **argv)
{
	struct option daemon_options[] = {
		OPT_INCR('v', "verbose", &verbose, "be more verbose"),
		OPT_STRING(0, "config", &__daemon.config,
			"config file", "config file path"),
		OPT_STRING(0, "base", &__daemon.base_user,
			"directory", "base directory"),
		OPT_END()
	};

	perf_exe(__daemon.perf, sizeof(__daemon.perf));
	__daemon.out = stdout;

	argc = parse_options(argc, argv, daemon_options, daemon_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc) {
		if (!strcmp(argv[0], "start"))
			return __cmd_start(&__daemon, daemon_options, argc, argv);

		pr_err("failed: unknown command '%s'\n", argv[0]);
		return -1;
	}

	if (setup_config(&__daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	return -1;
}
