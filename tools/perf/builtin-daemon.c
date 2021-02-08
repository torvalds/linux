// SPDX-License-Identifier: GPL-2.0
#include <internal/lib.h>
#include <subcmd/parse-options.h>
#include <api/fd/array.h>
#include <linux/zalloc.h>
#include <linux/string.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/inotify.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/stat.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "config.h"
#include "util.h"

#define SESSION_OUTPUT  "output"

/*
 * Session states:
 *
 *   OK       - session is up and running
 *   RECONFIG - session is pending for reconfiguration,
 *              new values are already loaded in session object
 *   KILL     - session is pending to be killed
 *
 * Session object life and its state is maintained by
 * following functions:
 *
 *  setup_server_config
 *    - reads config file and setup session objects
 *      with following states:
 *
 *      OK       - no change needed
 *      RECONFIG - session needs to be changed
 *                 (run variable changed)
 *      KILL     - session needs to be killed
 *                 (session is no longer in config file)
 *
 *  daemon__reconfig
 *    - scans session objects and does following actions
 *      for states:
 *
 *      OK       - skip
 *      RECONFIG - session is killed and re-run with new config
 *      KILL     - session is killed
 *
 *    - all sessions have OK state on the function exit
 */
enum daemon_session_state {
	OK,
	RECONFIG,
	KILL,
};

struct daemon_session {
	char				*base;
	char				*name;
	char				*run;
	int				 pid;
	struct list_head		 list;
	enum daemon_session_state	 state;
};

struct daemon {
	const char		*config;
	char			*config_real;
	char			*config_base;
	const char		*base_user;
	char			*base;
	struct list_head	 sessions;
	FILE			*out;
	char			 perf[PATH_MAX];
	int			 signal_fd;
};

static struct daemon __daemon = {
	.sessions = LIST_HEAD_INIT(__daemon.sessions),
};

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

static struct daemon_session *daemon__add_session(struct daemon *config, char *name)
{
	struct daemon_session *session = zalloc(sizeof(*session));

	if (!session)
		return NULL;

	session->name = strdup(name);
	if (!session->name) {
		free(session);
		return NULL;
	}

	session->pid = -1;
	list_add_tail(&session->list, &config->sessions);
	return session;
}

static struct daemon_session *daemon__find_session(struct daemon *daemon, char *name)
{
	struct daemon_session *session;

	list_for_each_entry(session, &daemon->sessions, list) {
		if (!strcmp(session->name, name))
			return session;
	}

	return NULL;
}

static int get_session_name(const char *var, char *session, int len)
{
	const char *p = var + sizeof("session-") - 1;

	while (*p != '.' && *p != 0x0 && len--)
		*session++ = *p++;

	*session = 0;
	return *p == '.' ? 0 : -EINVAL;
}

static int session_config(struct daemon *daemon, const char *var, const char *value)
{
	struct daemon_session *session;
	char name[100];

	if (get_session_name(var, name, sizeof(name)))
		return -EINVAL;

	var = strchr(var, '.');
	if (!var)
		return -EINVAL;

	var++;

	session = daemon__find_session(daemon, name);

	if (!session) {
		/* New session is defined. */
		session = daemon__add_session(daemon, name);
		if (!session)
			return -ENOMEM;

		pr_debug("reconfig: found new session %s\n", name);

		/* Trigger reconfig to start it. */
		session->state = RECONFIG;
	} else if (session->state == KILL) {
		/* Current session is defined, no action needed. */
		pr_debug("reconfig: found current session %s\n", name);
		session->state = OK;
	}

	if (!strcmp(var, "run")) {
		bool same = false;

		if (session->run)
			same = !strcmp(session->run, value);

		if (!same) {
			if (session->run) {
				free(session->run);
				pr_debug("reconfig: session %s is changed\n", name);
			}

			session->run = strdup(value);
			if (!session->run)
				return -ENOMEM;

			/*
			 * Either new or changed run value is defined,
			 * trigger reconfig for the session.
			 */
			session->state = RECONFIG;
		}
	}

	return 0;
}

static int server_config(const char *var, const char *value, void *cb)
{
	struct daemon *daemon = cb;

	if (strstarts(var, "session-")) {
		return session_config(daemon, var, value);
	} else if (!strcmp(var, "daemon.base") && !daemon->base_user) {
		if (daemon->base && strcmp(daemon->base, value)) {
			pr_err("failed: can't redefine base, bailing out\n");
			return -EINVAL;
		}
		daemon->base = strdup(value);
		if (!daemon->base)
			return -ENOMEM;
	}

	return 0;
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

static int setup_server_config(struct daemon *daemon)
{
	struct perf_config_set *set;
	struct daemon_session *session;
	int err = -ENOMEM;

	pr_debug("reconfig: started\n");

	/*
	 * Mark all sessions for kill, the server config
	 * will set following states, see explanation at
	 * enum daemon_session_state declaration.
	 */
	list_for_each_entry(session, &daemon->sessions, list)
		session->state = KILL;

	set = perf_config_set__load_file(daemon->config_real);
	if (set) {
		err = perf_config_set(set, server_config, daemon);
		perf_config_set__delete(set);
	}

	return err ?: check_base(daemon);
}

static int daemon_session__run(struct daemon_session *session,
			       struct daemon *daemon)
{
	char buf[PATH_MAX];
	char **argv;
	int argc, fd;

	if (asprintf(&session->base, "%s/session-%s",
		     daemon->base, session->name) < 0) {
		perror("failed: asprintf");
		return -1;
	}

	if (mkdir(session->base, 0755) && errno != EEXIST) {
		perror("failed: mkdir");
		return -1;
	}

	session->pid = fork();
	if (session->pid < 0)
		return -1;
	if (session->pid > 0) {
		pr_info("reconfig: ruining session [%s:%d]: %s\n",
			session->name, session->pid, session->run);
		return 0;
	}

	if (chdir(session->base)) {
		perror("failed: chdir");
		return -1;
	}

	fd = open("/dev/null", O_RDONLY);
	if (fd < 0) {
		perror("failed: open /dev/null");
		return -1;
	}

	dup2(fd, 0);
	close(fd);

	fd = open(SESSION_OUTPUT, O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (fd < 0) {
		perror("failed: open session output");
		return -1;
	}

	dup2(fd, 1);
	dup2(fd, 2);
	close(fd);

	scnprintf(buf, sizeof(buf), "%s record %s", daemon->perf, session->run);

	argv = argv_split(buf, &argc);
	if (!argv)
		exit(-1);

	exit(execve(daemon->perf, argv, NULL));
	return -1;
}

static pid_t handle_signalfd(struct daemon *daemon)
{
	struct daemon_session *session;
	struct signalfd_siginfo si;
	ssize_t err;
	int status;
	pid_t pid;

	err = read(daemon->signal_fd, &si, sizeof(struct signalfd_siginfo));
	if (err != sizeof(struct signalfd_siginfo))
		return -1;

	list_for_each_entry(session, &daemon->sessions, list) {

		if (session->pid != (int) si.ssi_pid)
			continue;

		pid = waitpid(session->pid, &status, 0);
		if (pid == session->pid) {
			if (WIFEXITED(status)) {
				pr_info("session '%s' exited, status=%d\n",
					session->name, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				pr_info("session '%s' killed (signal %d)\n",
					session->name, WTERMSIG(status));
			} else if (WIFSTOPPED(status)) {
				pr_info("session '%s' stopped (signal %d)\n",
					session->name, WSTOPSIG(status));
			} else {
				pr_info("session '%s' Unexpected status (0x%x)\n",
					session->name, status);
			}
		}

		session->state = KILL;
		session->pid = -1;
		return pid;
	}

	return 0;
}

static int daemon_session__wait(struct daemon_session *session, struct daemon *daemon,
				int secs)
{
	struct pollfd pollfd = {
		.fd	= daemon->signal_fd,
		.events	= POLLIN,
	};
	pid_t wpid = 0, pid = session->pid;
	time_t start;

	start = time(NULL);

	do {
		int err = poll(&pollfd, 1, 1000);

		if (err > 0) {
			wpid = handle_signalfd(daemon);
		} else if (err < 0) {
			perror("failed: poll\n");
			return -1;
		}

		if (start + secs < time(NULL))
			return -1;
	} while (wpid != pid);

	return 0;
}

static bool daemon__has_alive_session(struct daemon *daemon)
{
	struct daemon_session *session;

	list_for_each_entry(session, &daemon->sessions, list) {
		if (session->pid != -1)
			return true;
	}

	return false;
}

static int daemon__wait(struct daemon *daemon, int secs)
{
	struct pollfd pollfd = {
		.fd	= daemon->signal_fd,
		.events	= POLLIN,
	};
	time_t start;

	start = time(NULL);

	do {
		int err = poll(&pollfd, 1, 1000);

		if (err > 0) {
			handle_signalfd(daemon);
		} else if (err < 0) {
			perror("failed: poll\n");
			return -1;
		}

		if (start + secs < time(NULL))
			return -1;
	} while (daemon__has_alive_session(daemon));

	return 0;
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

static int daemon_session__signal(struct daemon_session *session, int sig)
{
	if (session->pid < 0)
		return -1;
	return kill(session->pid, sig);
}

static void daemon_session__kill(struct daemon_session *session,
				 struct daemon *daemon)
{
	daemon_session__signal(session, SIGTERM);
	if (daemon_session__wait(session, daemon, 10)) {
		daemon_session__signal(session, SIGKILL);
		daemon_session__wait(session, daemon, 10);
	}
}

static void daemon__signal(struct daemon *daemon, int sig)
{
	struct daemon_session *session;

	list_for_each_entry(session, &daemon->sessions, list)
		daemon_session__signal(session, sig);
}

static void daemon_session__delete(struct daemon_session *session)
{
	free(session->base);
	free(session->name);
	free(session->run);
	free(session);
}

static void daemon_session__remove(struct daemon_session *session)
{
	list_del(&session->list);
	daemon_session__delete(session);
}

static void daemon__kill(struct daemon *daemon)
{
	daemon__signal(daemon, SIGTERM);
	if (daemon__wait(daemon, 10)) {
		daemon__signal(daemon, SIGKILL);
		daemon__wait(daemon, 10);
	}
}

static void daemon__exit(struct daemon *daemon)
{
	struct daemon_session *session, *h;

	list_for_each_entry_safe(session, h, &daemon->sessions, list)
		daemon_session__remove(session);

	free(daemon->config_real);
	free(daemon->config_base);
	free(daemon->base);
}

static int daemon__reconfig(struct daemon *daemon)
{
	struct daemon_session *session, *n;

	list_for_each_entry_safe(session, n, &daemon->sessions, list) {
		/* No change. */
		if (session->state == OK)
			continue;

		/* Remove session. */
		if (session->state == KILL) {
			if (session->pid > 0) {
				daemon_session__kill(session, daemon);
				pr_info("reconfig: session '%s' killed\n", session->name);
			}
			daemon_session__remove(session);
			continue;
		}

		/* Reconfig session. */
		if (session->pid > 0) {
			daemon_session__kill(session, daemon);
			pr_info("reconfig: session '%s' killed\n", session->name);
		}
		if (daemon_session__run(session, daemon))
			return -1;

		session->state = OK;
	}

	return 0;
}

static int setup_config_changes(struct daemon *daemon)
{
	char *basen = strdup(daemon->config_real);
	char *dirn  = strdup(daemon->config_real);
	char *base, *dir;
	int fd, wd = -1;

	if (!dirn || !basen)
		goto out;

	fd = inotify_init1(IN_NONBLOCK|O_CLOEXEC);
	if (fd < 0) {
		perror("failed: inotify_init");
		goto out;
	}

	dir = dirname(dirn);
	base = basename(basen);
	pr_debug("config file: %s, dir: %s\n", base, dir);

	wd = inotify_add_watch(fd, dir, IN_CLOSE_WRITE);
	if (wd >= 0) {
		daemon->config_base = strdup(base);
		if (!daemon->config_base) {
			close(fd);
			wd = -1;
		}
	} else {
		perror("failed: inotify_add_watch");
	}

out:
	free(basen);
	free(dirn);
	return wd < 0 ? -1 : fd;
}

static bool process_inotify_event(struct daemon *daemon, char *buf, ssize_t len)
{
	char *p = buf;

	while (p < (buf + len)) {
		struct inotify_event *event = (struct inotify_event *) p;

		/*
		 * We monitor config directory, check if our
		 * config file was changes.
		 */
		if ((event->mask & IN_CLOSE_WRITE) &&
		    !(event->mask & IN_ISDIR)) {
			if (!strcmp(event->name, daemon->config_base))
				return true;
		}
		p += sizeof(*event) + event->len;
	}
	return false;
}

static int handle_config_changes(struct daemon *daemon, int conf_fd,
				 bool *config_changed)
{
	char buf[4096];
	ssize_t len;

	while (!(*config_changed)) {
		len = read(conf_fd, buf, sizeof(buf));
		if (len == -1) {
			if (errno != EAGAIN) {
				perror("failed: read");
				return -1;
			}
			return 0;
		}
		*config_changed = process_inotify_event(daemon, buf, len);
	}
	return 0;
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

static int go_background(struct daemon *daemon)
{
	int pid, fd;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid > 0)
		return 1;

	if (setsid() < 0)
		return -1;

	umask(0);

	if (chdir(daemon->base)) {
		perror("failed: chdir");
		return -1;
	}

	fd = open("output", O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (fd < 0) {
		perror("failed: open");
		return -1;
	}

	if (fcntl(fd, F_SETFD, FD_CLOEXEC)) {
		perror("failed: fcntl FD_CLOEXEC");
		close(fd);
		return -1;
	}

	close(0);
	dup2(fd, 1);
	dup2(fd, 2);
	close(fd);

	daemon->out = fdopen(1, "w");
	if (!daemon->out) {
		close(1);
		close(2);
		return -1;
	}

	setbuf(daemon->out, NULL);
	return 0;
}

static int setup_signalfd(struct daemon *daemon)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
		return -1;

	daemon->signal_fd = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
	return daemon->signal_fd;
}

static int __cmd_start(struct daemon *daemon, struct option parent_options[],
		       int argc, const char **argv)
{
	bool foreground = false;
	struct option start_options[] = {
		OPT_BOOLEAN('f', "foreground", &foreground, "stay on console"),
		OPT_PARENT(parent_options),
		OPT_END()
	};
	int sock_fd = -1, conf_fd = -1, signal_fd = -1;
	int sock_pos, file_pos, signal_pos;
	struct fdarray fda;
	int err = 0;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	if (setup_server_config(daemon))
		return -1;

	if (!foreground) {
		err = go_background(daemon);
		if (err) {
			/* original process, exit normally */
			if (err == 1)
				err = 0;
			daemon__exit(daemon);
			return err;
		}
	}

	debug_set_file(daemon->out);
	debug_set_display_time(true);

	pr_info("daemon started (pid %d)\n", getpid());

	fdarray__init(&fda, 3);

	sock_fd = setup_server_socket(daemon);
	if (sock_fd < 0)
		goto out;

	conf_fd = setup_config_changes(daemon);
	if (conf_fd < 0)
		goto out;

	signal_fd = setup_signalfd(daemon);
	if (signal_fd < 0)
		goto out;

	sock_pos = fdarray__add(&fda, sock_fd, POLLIN|POLLERR|POLLHUP, 0);
	if (sock_pos < 0)
		goto out;

	file_pos = fdarray__add(&fda, conf_fd, POLLIN|POLLERR|POLLHUP, 0);
	if (file_pos < 0)
		goto out;

	signal_pos = fdarray__add(&fda, signal_fd, POLLIN|POLLERR|POLLHUP, 0);
	if (signal_pos < 0)
		goto out;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	while (!done && !err) {
		err = daemon__reconfig(daemon);

		if (!err && fdarray__poll(&fda, -1)) {
			bool reconfig = false;

			if (fda.entries[sock_pos].revents & POLLIN)
				err = handle_server_socket(daemon, sock_fd);
			if (fda.entries[file_pos].revents & POLLIN)
				err = handle_config_changes(daemon, conf_fd, &reconfig);
			if (fda.entries[signal_pos].revents & POLLIN)
				err = handle_signalfd(daemon) < 0;

			if (reconfig)
				err = setup_server_config(daemon);
		}
	}

out:
	fdarray__exit(&fda);

	daemon__kill(daemon);
	daemon__exit(daemon);

	if (sock_fd != -1)
		close(sock_fd);
	if (conf_fd != -1)
		close(conf_fd);
	if (conf_fd != -1)
		close(signal_fd);

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
