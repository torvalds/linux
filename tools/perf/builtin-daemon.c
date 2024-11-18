// SPDX-License-Identifier: GPL-2.0
#include <internal/lib.h>
#include <inttypes.h>
#include <subcmd/parse-options.h>
#include <api/fd/array.h>
#include <api/fs/fs.h>
#include <linux/zalloc.h>
#include <linux/string.h>
#include <linux/limits.h>
#include <string.h>
#include <sys/file.h>
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
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "config.h"
#include "util.h"

#define SESSION_OUTPUT  "output"
#define SESSION_CONTROL "control"
#define SESSION_ACK     "ack"

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
	char				*control;
	int				 pid;
	struct list_head		 list;
	enum daemon_session_state	 state;
	time_t				 start;
};

struct daemon {
	const char		*config;
	char			*config_real;
	char			*config_base;
	const char		*csv_sep;
	const char		*base_user;
	char			*base;
	struct list_head	 sessions;
	FILE			*out;
	char			*perf;
	int			 signal_fd;
	time_t			 start;
};

static struct daemon __daemon = {
	.sessions = LIST_HEAD_INIT(__daemon.sessions),
};

static const char * const daemon_usage[] = {
	"perf daemon {start|signal|stop|ping} [<options>]",
	"perf daemon [<options>]",
	NULL
};

static volatile sig_atomic_t done;

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

	if (get_session_name(var, name, sizeof(name) - 1))
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
				zfree(&session->run);
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

	session->start = time(NULL);

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

	if (mkfifo(SESSION_CONTROL, 0600) && errno != EEXIST) {
		perror("failed: create control fifo");
		return -1;
	}

	if (mkfifo(SESSION_ACK, 0600) && errno != EEXIST) {
		perror("failed: create ack fifo");
		return -1;
	}

	scnprintf(buf, sizeof(buf), "%s record --control=fifo:%s,%s %s",
		  daemon->perf, SESSION_CONTROL, SESSION_ACK, session->run);

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

	/*
	 * Take signal fd data as pure signal notification and check all
	 * the sessions state. The reason is that multiple signals can get
	 * coalesced in kernel and we can receive only single signal even
	 * if multiple SIGCHLD were generated.
	 */
	err = read(daemon->signal_fd, &si, sizeof(struct signalfd_siginfo));
	if (err != sizeof(struct signalfd_siginfo)) {
		pr_err("failed to read signal fd\n");
		return -1;
	}

	list_for_each_entry(session, &daemon->sessions, list) {
		if (session->pid == -1)
			continue;

		pid = waitpid(session->pid, &status, WNOHANG);
		if (pid <= 0)
			continue;

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

		session->state = KILL;
		session->pid = -1;
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
	} while (session->pid != -1);

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

static int daemon_session__control(struct daemon_session *session,
				   const char *msg, bool do_ack)
{
	struct pollfd pollfd = { .events = POLLIN, };
	char control_path[PATH_MAX];
	char ack_path[PATH_MAX];
	int control, ack = -1, len;
	char buf[20];
	int ret = -1;
	ssize_t err;

	/* open the control file */
	scnprintf(control_path, sizeof(control_path), "%s/%s",
		  session->base, SESSION_CONTROL);

	control = open(control_path, O_WRONLY|O_NONBLOCK);
	if (control < 0)
		return -1;

	if (do_ack) {
		/* open the ack file */
		scnprintf(ack_path, sizeof(ack_path), "%s/%s",
			  session->base, SESSION_ACK);

		ack = open(ack_path, O_RDONLY, O_NONBLOCK);
		if (ack < 0) {
			close(control);
			return -1;
		}
	}

	/* write the command */
	len = strlen(msg);

	err = writen(control, msg, len);
	if (err != len) {
		pr_err("failed: write to control pipe: %d (%s)\n",
		       errno, control_path);
		goto out;
	}

	if (!do_ack)
		goto out;

	/* wait for an ack */
	pollfd.fd = ack;

	if (!poll(&pollfd, 1, 2000)) {
		pr_err("failed: control ack timeout\n");
		goto out;
	}

	if (!(pollfd.revents & POLLIN)) {
		pr_err("failed: did not received an ack\n");
		goto out;
	}

	err = read(ack, buf, sizeof(buf));
	if (err > 0)
		ret = strcmp(buf, "ack\n");
	else
		perror("failed: read ack %d\n");

out:
	if (ack != -1)
		close(ack);

	close(control);
	return ret;
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

enum {
	CMD_LIST   = 0,
	CMD_SIGNAL = 1,
	CMD_STOP   = 2,
	CMD_PING   = 3,
	CMD_MAX,
};

#define SESSION_MAX 64

union cmd {
	int cmd;

	/* CMD_LIST */
	struct {
		int	cmd;
		int	verbose;
		char	csv_sep;
	} list;

	/* CMD_SIGNAL */
	struct {
		int	cmd;
		int	sig;
		char	name[SESSION_MAX];
	} signal;

	/* CMD_PING */
	struct {
		int	cmd;
		char	name[SESSION_MAX];
	} ping;
};

enum {
	PING_OK	  = 0,
	PING_FAIL = 1,
	PING_MAX,
};

static int daemon_session__ping(struct daemon_session *session)
{
	return daemon_session__control(session, "ping", true) ?  PING_FAIL : PING_OK;
}

static int cmd_session_list(struct daemon *daemon, union cmd *cmd, FILE *out)
{
	char csv_sep = cmd->list.csv_sep;
	struct daemon_session *session;
	time_t curr = time(NULL);

	if (csv_sep) {
		fprintf(out, "%d%c%s%c%s%c%s/%s",
			/* pid daemon  */
			getpid(), csv_sep, "daemon",
			/* base */
			csv_sep, daemon->base,
			/* output */
			csv_sep, daemon->base, SESSION_OUTPUT);

		fprintf(out, "%c%s/%s",
			/* lock */
			csv_sep, daemon->base, "lock");

		fprintf(out, "%c%" PRIu64,
			/* session up time */
			csv_sep, (uint64_t)((curr - daemon->start) / 60));

		fprintf(out, "\n");
	} else {
		fprintf(out, "[%d:daemon] base: %s\n", getpid(), daemon->base);
		if (cmd->list.verbose) {
			fprintf(out, "  output:  %s/%s\n",
				daemon->base, SESSION_OUTPUT);
			fprintf(out, "  lock:    %s/lock\n",
				daemon->base);
			fprintf(out, "  up:      %" PRIu64 " minutes\n",
				(uint64_t)((curr - daemon->start) / 60));
		}
	}

	list_for_each_entry(session, &daemon->sessions, list) {
		if (csv_sep) {
			fprintf(out, "%d%c%s%c%s",
				/* pid */
				session->pid,
				/* name */
				csv_sep, session->name,
				/* base */
				csv_sep, session->run);

			fprintf(out, "%c%s%c%s/%s",
				/* session dir */
				csv_sep, session->base,
				/* session output */
				csv_sep, session->base, SESSION_OUTPUT);

			fprintf(out, "%c%s/%s%c%s/%s",
				/* session control */
				csv_sep, session->base, SESSION_CONTROL,
				/* session ack */
				csv_sep, session->base, SESSION_ACK);

			fprintf(out, "%c%" PRIu64,
				/* session up time */
				csv_sep, (uint64_t)((curr - session->start) / 60));

			fprintf(out, "\n");
		} else {
			fprintf(out, "[%d:%s] perf record %s\n",
				session->pid, session->name, session->run);
			if (!cmd->list.verbose)
				continue;
			fprintf(out, "  base:    %s\n",
				session->base);
			fprintf(out, "  output:  %s/%s\n",
				session->base, SESSION_OUTPUT);
			fprintf(out, "  control: %s/%s\n",
				session->base, SESSION_CONTROL);
			fprintf(out, "  ack:     %s/%s\n",
				session->base, SESSION_ACK);
			fprintf(out, "  up:      %" PRIu64 " minutes\n",
				(uint64_t)((curr - session->start) / 60));
		}
	}

	return 0;
}

static int daemon_session__signal(struct daemon_session *session, int sig)
{
	if (session->pid < 0)
		return -1;
	return kill(session->pid, sig);
}

static int cmd_session_kill(struct daemon *daemon, union cmd *cmd, FILE *out)
{
	struct daemon_session *session;
	bool all = false;

	all = !strcmp(cmd->signal.name, "all");

	list_for_each_entry(session, &daemon->sessions, list) {
		if (all || !strcmp(cmd->signal.name, session->name)) {
			daemon_session__signal(session, cmd->signal.sig);
			fprintf(out, "signal %d sent to session '%s [%d]'\n",
				cmd->signal.sig, session->name, session->pid);
		}
	}

	return 0;
}

static const char *ping_str[PING_MAX] = {
	[PING_OK]   = "OK",
	[PING_FAIL] = "FAIL",
};

static int cmd_session_ping(struct daemon *daemon, union cmd *cmd, FILE *out)
{
	struct daemon_session *session;
	bool all = false, found = false;

	all = !strcmp(cmd->ping.name, "all");

	list_for_each_entry(session, &daemon->sessions, list) {
		if (all || !strcmp(cmd->ping.name, session->name)) {
			int state = daemon_session__ping(session);

			fprintf(out, "%-4s %s\n", ping_str[state], session->name);
			found = true;
		}
	}

	if (!found && !all) {
		fprintf(out, "%-4s %s (not found)\n",
			ping_str[PING_FAIL], cmd->ping.name);
	}
	return 0;
}

static int handle_server_socket(struct daemon *daemon, int sock_fd)
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
	case CMD_LIST:
		ret = cmd_session_list(daemon, &cmd, out);
		break;
	case CMD_SIGNAL:
		ret = cmd_session_kill(daemon, &cmd, out);
		break;
	case CMD_STOP:
		done = 1;
		ret = 0;
		pr_debug("perf daemon is exciting\n");
		break;
	case CMD_PING:
		ret = cmd_session_ping(daemon, &cmd, out);
		break;
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

static void daemon_session__kill(struct daemon_session *session,
				 struct daemon *daemon)
{
	int how = 0;

	do {
		switch (how) {
		case 0:
			daemon_session__control(session, "stop", false);
			break;
		case 1:
			daemon_session__signal(session, SIGTERM);
			break;
		case 2:
			daemon_session__signal(session, SIGKILL);
			break;
		default:
			pr_err("failed to wait for session %s\n",
			       session->name);
			return;
		}
		how++;

	} while (daemon_session__wait(session, daemon, 10));
}

static void daemon__signal(struct daemon *daemon, int sig)
{
	struct daemon_session *session;

	list_for_each_entry(session, &daemon->sessions, list)
		daemon_session__signal(session, sig);
}

static void daemon_session__delete(struct daemon_session *session)
{
	zfree(&session->base);
	zfree(&session->name);
	zfree(&session->run);
	free(session);
}

static void daemon_session__remove(struct daemon_session *session)
{
	list_del(&session->list);
	daemon_session__delete(session);
}

static void daemon__stop(struct daemon *daemon)
{
	struct daemon_session *session;

	list_for_each_entry(session, &daemon->sessions, list)
		daemon_session__control(session, "stop", false);
}

static void daemon__kill(struct daemon *daemon)
{
	int how = 0;

	do {
		switch (how) {
		case 0:
			daemon__stop(daemon);
			break;
		case 1:
			daemon__signal(daemon, SIGTERM);
			break;
		case 2:
			daemon__signal(daemon, SIGKILL);
			break;
		default:
			pr_err("failed to wait for sessions\n");
			return;
		}
		how++;

	} while (daemon__wait(daemon, 10));
}

static void daemon__exit(struct daemon *daemon)
{
	struct daemon_session *session, *h;

	list_for_each_entry_safe(session, h, &daemon->sessions, list)
		daemon_session__remove(session);

	zfree(&daemon->config_real);
	zfree(&daemon->config_base);
	zfree(&daemon->base);
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

#ifndef F_TLOCK
#define F_TLOCK 2

static int lockf(int fd, int cmd, off_t len)
{
	if (cmd != F_TLOCK || len != 0)
		return -1;

	return flock(fd, LOCK_EX | LOCK_NB);
}
#endif // F_TLOCK

/*
 * Each daemon tries to create and lock BASE/lock file,
 * if it's successful we are sure we're the only daemon
 * running over the BASE.
 *
 * Once daemon is finished, file descriptor to lock file
 * is closed and lock is released.
 */
static int check_lock(struct daemon *daemon)
{
	char path[PATH_MAX];
	char buf[20];
	int fd, pid;
	ssize_t len;

	scnprintf(path, sizeof(path), "%s/lock", daemon->base);

	fd = open(path, O_RDWR|O_CREAT|O_CLOEXEC, 0640);
	if (fd < 0)
		return -1;

	if (lockf(fd, F_TLOCK, 0) < 0) {
		filename__read_int(path, &pid);
		fprintf(stderr, "failed: another perf daemon (pid %d) owns %s\n",
			pid, daemon->base);
		close(fd);
		return -1;
	}

	scnprintf(buf, sizeof(buf), "%d", getpid());
	len = strlen(buf);

	if (write(fd, buf, len) != len) {
		perror("failed: write");
		close(fd);
		return -1;
	}

	if (ftruncate(fd, len)) {
		perror("failed: ftruncate");
		close(fd);
		return -1;
	}

	return 0;
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

	if (check_lock(daemon))
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

	daemon->start = time(NULL);

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	if (setup_server_config(daemon))
		return -1;

	if (foreground && check_lock(daemon))
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
	signal(SIGPIPE, SIG_IGN);

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
	if (signal_fd != -1)
		close(signal_fd);

	pr_info("daemon exited\n");
	fclose(daemon->out);
	return err;
}

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

static int send_cmd_list(struct daemon *daemon)
{
	union cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.list.cmd = CMD_LIST;
	cmd.list.verbose = verbose;
	cmd.list.csv_sep = daemon->csv_sep ? *daemon->csv_sep : 0;

	return send_cmd(daemon, &cmd);
}

static int __cmd_signal(struct daemon *daemon, struct option parent_options[],
			int argc, const char **argv)
{
	const char *name = "all";
	struct option start_options[] = {
		OPT_STRING(0, "session", &name, "session",
			"Sent signal to specific session"),
		OPT_PARENT(parent_options),
		OPT_END()
	};
	union cmd cmd;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.signal.cmd = CMD_SIGNAL;
	cmd.signal.sig = SIGUSR2;
	strncpy(cmd.signal.name, name, sizeof(cmd.signal.name) - 1);

	return send_cmd(daemon, &cmd);
}

static int __cmd_stop(struct daemon *daemon, struct option parent_options[],
			int argc, const char **argv)
{
	struct option start_options[] = {
		OPT_PARENT(parent_options),
		OPT_END()
	};
	union cmd cmd;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = CMD_STOP;
	return send_cmd(daemon, &cmd);
}

static int __cmd_ping(struct daemon *daemon, struct option parent_options[],
		      int argc, const char **argv)
{
	const char *name = "all";
	struct option ping_options[] = {
		OPT_STRING(0, "session", &name, "session",
			"Ping to specific session"),
		OPT_PARENT(parent_options),
		OPT_END()
	};
	union cmd cmd;

	argc = parse_options(argc, argv, ping_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, ping_options);

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = CMD_PING;
	scnprintf(cmd.ping.name, sizeof(cmd.ping.name), "%s", name);
	return send_cmd(daemon, &cmd);
}

static char *alloc_perf_exe_path(void)
{
	char path[PATH_MAX];

	perf_exe(path, sizeof(path));
	return strdup(path);
}

int cmd_daemon(int argc, const char **argv)
{
	struct option daemon_options[] = {
		OPT_INCR('v', "verbose", &verbose, "be more verbose"),
		OPT_STRING(0, "config", &__daemon.config,
			"config file", "config file path"),
		OPT_STRING(0, "base", &__daemon.base_user,
			"directory", "base directory"),
		OPT_STRING_OPTARG('x', "field-separator", &__daemon.csv_sep,
			"field separator", "print counts with custom separator", ","),
		OPT_END()
	};
	int ret = -1;

	__daemon.perf = alloc_perf_exe_path();
	if (!__daemon.perf)
		return -ENOMEM;

	__daemon.out = stdout;

	argc = parse_options(argc, argv, daemon_options, daemon_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc) {
		if (!strcmp(argv[0], "start"))
			ret = __cmd_start(&__daemon, daemon_options, argc, argv);
		else if (!strcmp(argv[0], "signal"))
			ret = __cmd_signal(&__daemon, daemon_options, argc, argv);
		else if (!strcmp(argv[0], "stop"))
			ret = __cmd_stop(&__daemon, daemon_options, argc, argv);
		else if (!strcmp(argv[0], "ping"))
			ret = __cmd_ping(&__daemon, daemon_options, argc, argv);
		else
			pr_err("failed: unknown command '%s'\n", argv[0]);
	} else {
		ret = setup_config(&__daemon);
		if (ret)
			pr_err("failed: config not found\n");
		else
			ret = send_cmd_list(&__daemon);
	}
	zfree(&__daemon.perf);
	return ret;
}
