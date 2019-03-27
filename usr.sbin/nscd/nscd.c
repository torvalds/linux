/*-
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in thereg
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "agents/passwd.h"
#include "agents/group.h"
#include "agents/services.h"
#include "cachelib.h"
#include "config.h"
#include "debug.h"
#include "log.h"
#include "nscdcli.h"
#include "parser.h"
#include "query.h"
#include "singletons.h"

#ifndef CONFIG_PATH
#define CONFIG_PATH "/etc/nscd.conf"
#endif
#define DEFAULT_CONFIG_PATH	"nscd.conf"

#define MAX_SOCKET_IO_SIZE	4096

struct processing_thread_args {
	cache	the_cache;
	struct configuration	*the_configuration;
	struct runtime_env		*the_runtime_env;
};

static void accept_connection(struct kevent *, struct runtime_env *,
	struct configuration *);
static void destroy_cache_(cache);
static void destroy_runtime_env(struct runtime_env *);
static cache init_cache_(struct configuration *);
static struct runtime_env *init_runtime_env(struct configuration *);
static void processing_loop(cache, struct runtime_env *,
	struct configuration *);
static void process_socket_event(struct kevent *, struct runtime_env *,
	struct configuration *);
static void process_timer_event(struct kevent *, struct runtime_env *,
	struct configuration *);
static void *processing_thread(void *);
static void usage(void);

void get_time_func(struct timeval *);

static void
usage(void)
{
	fprintf(stderr,
	    "usage: nscd [-dnst] [-i cachename] [-I cachename]\n");
	exit(1);
}

static cache
init_cache_(struct configuration *config)
{
	struct cache_params params;
	cache retval;

	struct configuration_entry *config_entry;
	size_t	size, i;
	int res;

	TRACE_IN(init_cache_);

	memset(&params, 0, sizeof(struct cache_params));
	params.get_time_func = get_time_func;
	retval = init_cache(&params);

	size = configuration_get_entries_size(config);
	for (i = 0; i < size; ++i) {
		config_entry = configuration_get_entry(config, i);
	    	/*
	    	 * We should register common entries now - multipart entries
	    	 * would be registered automatically during the queries.
	    	 */
		res = register_cache_entry(retval, (struct cache_entry_params *)
			&config_entry->positive_cache_params);
		config_entry->positive_cache_entry = find_cache_entry(retval,
			config_entry->positive_cache_params.cep.entry_name);
		assert(config_entry->positive_cache_entry !=
			INVALID_CACHE_ENTRY);

		res = register_cache_entry(retval, (struct cache_entry_params *)
			&config_entry->negative_cache_params);
		config_entry->negative_cache_entry = find_cache_entry(retval,
			config_entry->negative_cache_params.cep.entry_name);
		assert(config_entry->negative_cache_entry !=
			INVALID_CACHE_ENTRY);
	}

	LOG_MSG_2("cache", "cache was successfully initialized");
	TRACE_OUT(init_cache_);
	return (retval);
}

static void
destroy_cache_(cache the_cache)
{
	TRACE_IN(destroy_cache_);
	destroy_cache(the_cache);
	TRACE_OUT(destroy_cache_);
}

/*
 * Socket and kqueues are prepared here. We have one global queue for both
 * socket and timers events.
 */
static struct runtime_env *
init_runtime_env(struct configuration *config)
{
	int serv_addr_len;
	struct sockaddr_un serv_addr;

	struct kevent eventlist;
	struct timespec timeout;

	struct runtime_env *retval;

	TRACE_IN(init_runtime_env);
	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	retval->sockfd = socket(PF_LOCAL, SOCK_STREAM, 0);

	if (config->force_unlink == 1)
		unlink(config->socket_path);

	memset(&serv_addr, 0, sizeof(struct sockaddr_un));
	serv_addr.sun_family = PF_LOCAL;
	strlcpy(serv_addr.sun_path, config->socket_path,
		sizeof(serv_addr.sun_path));
	serv_addr_len = sizeof(serv_addr.sun_family) +
		strlen(serv_addr.sun_path) + 1;

	if (bind(retval->sockfd, (struct sockaddr *)&serv_addr,
		serv_addr_len) == -1) {
		close(retval->sockfd);
		free(retval);

		LOG_ERR_2("runtime environment", "can't bind socket to path: "
			"%s", config->socket_path);
		TRACE_OUT(init_runtime_env);
		return (NULL);
	}
	LOG_MSG_2("runtime environment", "using socket %s",
		config->socket_path);

	/*
	 * Here we're marking socket as non-blocking and setting its backlog
	 * to the maximum value
	 */
	chmod(config->socket_path, config->socket_mode);
	listen(retval->sockfd, -1);
	fcntl(retval->sockfd, F_SETFL, O_NONBLOCK);

	retval->queue = kqueue();
	assert(retval->queue != -1);

	EV_SET(&eventlist, retval->sockfd, EVFILT_READ, EV_ADD | EV_ONESHOT,
		0, 0, 0);
	memset(&timeout, 0, sizeof(struct timespec));
	kevent(retval->queue, &eventlist, 1, NULL, 0, &timeout);

	LOG_MSG_2("runtime environment", "successfully initialized");
	TRACE_OUT(init_runtime_env);
	return (retval);
}

static void
destroy_runtime_env(struct runtime_env *env)
{
	TRACE_IN(destroy_runtime_env);
	close(env->queue);
	close(env->sockfd);
	free(env);
	TRACE_OUT(destroy_runtime_env);
}

static void
accept_connection(struct kevent *event_data, struct runtime_env *env,
	struct configuration *config)
{
	struct kevent	eventlist[2];
	struct timespec	timeout;
	struct query_state	*qstate;

	int	fd;
	int	res;

	uid_t	euid;
	gid_t	egid;

	TRACE_IN(accept_connection);
	fd = accept(event_data->ident, NULL, NULL);
	if (fd == -1) {
		LOG_ERR_2("accept_connection", "error %d during accept()",
		    errno);
		TRACE_OUT(accept_connection);
		return;
	}

	if (getpeereid(fd, &euid, &egid) != 0) {
		LOG_ERR_2("accept_connection", "error %d during getpeereid()",
			errno);
		TRACE_OUT(accept_connection);
		return;
	}

	qstate = init_query_state(fd, sizeof(int), euid, egid);
	if (qstate == NULL) {
		LOG_ERR_2("accept_connection", "can't init query_state");
		TRACE_OUT(accept_connection);
		return;
	}

	memset(&timeout, 0, sizeof(struct timespec));
	EV_SET(&eventlist[0], fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
		0, qstate->timeout.tv_sec * 1000, qstate);
	EV_SET(&eventlist[1], fd, EVFILT_READ, EV_ADD | EV_ONESHOT,
		NOTE_LOWAT, qstate->kevent_watermark, qstate);
	res = kevent(env->queue, eventlist, 2, NULL, 0, &timeout);
	if (res < 0)
		LOG_ERR_2("accept_connection", "kevent error");

	TRACE_OUT(accept_connection);
}

static void
process_socket_event(struct kevent *event_data, struct runtime_env *env,
	struct configuration *config)
{
	struct kevent	eventlist[2];
	struct timeval	query_timeout;
	struct timespec	kevent_timeout;
	int	nevents;
	int	eof_res, res;
	ssize_t	io_res;
	struct query_state *qstate;

	TRACE_IN(process_socket_event);
	eof_res = event_data->flags & EV_EOF ? 1 : 0;
	res = 0;

	memset(&kevent_timeout, 0, sizeof(struct timespec));
	EV_SET(&eventlist[0], event_data->ident, EVFILT_TIMER, EV_DELETE,
		0, 0, NULL);
	nevents = kevent(env->queue, eventlist, 1, NULL, 0, &kevent_timeout);
	if (nevents == -1) {
		if (errno == ENOENT) {
			/* the timer is already handling this event */
			TRACE_OUT(process_socket_event);
			return;
		} else {
			/* some other error happened */
			LOG_ERR_2("process_socket_event", "kevent error, errno"
				" is %d", errno);
			TRACE_OUT(process_socket_event);
			return;
		}
	}
	qstate = (struct query_state *)event_data->udata;

	/*
	 * If the buffer that is to be send/received is too large,
	 * we send it implicitly, by using query_io_buffer_read and
	 * query_io_buffer_write functions in the query_state. These functions
	 * use the temporary buffer, which is later send/received in parts.
	 * The code below implements buffer splitting/mergind for send/receive
	 * operations. It also does the actual socket IO operations.
	 */
	if (((qstate->use_alternate_io == 0) &&
		(qstate->kevent_watermark <= (size_t)event_data->data)) ||
		((qstate->use_alternate_io != 0) &&
		(qstate->io_buffer_watermark <= (size_t)event_data->data))) {
		if (qstate->use_alternate_io != 0) {
			switch (qstate->io_buffer_filter) {
			case EVFILT_READ:
				io_res = query_socket_read(qstate,
					qstate->io_buffer_p,
					qstate->io_buffer_watermark);
				if (io_res < 0) {
					qstate->use_alternate_io = 0;
					qstate->process_func = NULL;
				} else {
					qstate->io_buffer_p += io_res;
					if (qstate->io_buffer_p ==
					    	qstate->io_buffer +
						qstate->io_buffer_size) {
						qstate->io_buffer_p =
						    qstate->io_buffer;
						qstate->use_alternate_io = 0;
					}
				}
			break;
			default:
			break;
			}
		}

		if (qstate->use_alternate_io == 0) {
			do {
				res = qstate->process_func(qstate);
			} while ((qstate->kevent_watermark == 0) &&
					(qstate->process_func != NULL) &&
					(res == 0));

			if (res != 0)
				qstate->process_func = NULL;
		}

		if ((qstate->use_alternate_io != 0) &&
			(qstate->io_buffer_filter == EVFILT_WRITE)) {
			io_res = query_socket_write(qstate, qstate->io_buffer_p,
				qstate->io_buffer_watermark);
			if (io_res < 0) {
				qstate->use_alternate_io = 0;
				qstate->process_func = NULL;
			} else
				qstate->io_buffer_p += io_res;
		}
	} else {
		/* assuming that socket was closed */
		qstate->process_func = NULL;
		qstate->use_alternate_io = 0;
	}

	if (((qstate->process_func == NULL) &&
	    	(qstate->use_alternate_io == 0)) ||
		(eof_res != 0) || (res != 0)) {
		destroy_query_state(qstate);
		close(event_data->ident);
		TRACE_OUT(process_socket_event);
		return;
	}

	/* updating the query_state lifetime variable */
	get_time_func(&query_timeout);
	query_timeout.tv_usec = 0;
	query_timeout.tv_sec -= qstate->creation_time.tv_sec;
	if (query_timeout.tv_sec > qstate->timeout.tv_sec)
		query_timeout.tv_sec = 0;
	else
		query_timeout.tv_sec = qstate->timeout.tv_sec -
			query_timeout.tv_sec;

	if ((qstate->use_alternate_io != 0) && (qstate->io_buffer_p ==
		qstate->io_buffer + qstate->io_buffer_size))
		qstate->use_alternate_io = 0;

	if (qstate->use_alternate_io == 0) {
		/*
		 * If we must send/receive the large block of data,
		 * we should prepare the query_state's io_XXX fields.
		 * We should also substitute its write_func and read_func
		 * with the query_io_buffer_write and query_io_buffer_read,
		 * which will allow us to implicitly send/receive this large
		 * buffer later (in the subsequent calls to the
		 * process_socket_event).
		 */
		if (qstate->kevent_watermark > MAX_SOCKET_IO_SIZE) {
#if 0
			/*
			 * XXX: Uncommenting this code makes nscd(8) fail for
			 *      entries larger than a few kB, causing few second
			 *      worth of delay for each call to retrieve them.
			 */
			if (qstate->io_buffer != NULL)
				free(qstate->io_buffer);

			qstate->io_buffer = calloc(1,
				qstate->kevent_watermark);
			assert(qstate->io_buffer != NULL);

			qstate->io_buffer_p = qstate->io_buffer;
			qstate->io_buffer_size = qstate->kevent_watermark;
			qstate->io_buffer_filter = qstate->kevent_filter;

			qstate->write_func = query_io_buffer_write;
			qstate->read_func = query_io_buffer_read;

			if (qstate->kevent_filter == EVFILT_READ)
				qstate->use_alternate_io = 1;
#endif

			qstate->io_buffer_watermark = MAX_SOCKET_IO_SIZE;
			EV_SET(&eventlist[1], event_data->ident,
				qstate->kevent_filter, EV_ADD | EV_ONESHOT,
				NOTE_LOWAT, MAX_SOCKET_IO_SIZE, qstate);
		} else {
			EV_SET(&eventlist[1], event_data->ident,
		    		qstate->kevent_filter, EV_ADD | EV_ONESHOT,
		    		NOTE_LOWAT, qstate->kevent_watermark, qstate);
		}
	} else {
		if (qstate->io_buffer + qstate->io_buffer_size -
		    	qstate->io_buffer_p <
			MAX_SOCKET_IO_SIZE) {
			qstate->io_buffer_watermark = qstate->io_buffer +
				qstate->io_buffer_size - qstate->io_buffer_p;
			EV_SET(&eventlist[1], event_data->ident,
			    	qstate->io_buffer_filter,
				EV_ADD | EV_ONESHOT, NOTE_LOWAT,
				qstate->io_buffer_watermark,
				qstate);
		} else {
			qstate->io_buffer_watermark = MAX_SOCKET_IO_SIZE;
			EV_SET(&eventlist[1], event_data->ident,
		    		qstate->io_buffer_filter, EV_ADD | EV_ONESHOT,
		    		NOTE_LOWAT, MAX_SOCKET_IO_SIZE, qstate);
		}
	}
	EV_SET(&eventlist[0], event_data->ident, EVFILT_TIMER,
		EV_ADD | EV_ONESHOT, 0, query_timeout.tv_sec * 1000, qstate);
	kevent(env->queue, eventlist, 2, NULL, 0, &kevent_timeout);

	TRACE_OUT(process_socket_event);
}

/*
 * This routine is called if timer event has been signaled in the kqueue. It
 * just closes the socket and destroys the query_state.
 */
static void
process_timer_event(struct kevent *event_data, struct runtime_env *env,
	struct configuration *config)
{
	struct query_state	*qstate;

	TRACE_IN(process_timer_event);
	qstate = (struct query_state *)event_data->udata;
	destroy_query_state(qstate);
	close(event_data->ident);
	TRACE_OUT(process_timer_event);
}

/*
 * Processing loop is the basic processing routine, that forms a body of each
 * procssing thread
 */
static void
processing_loop(cache the_cache, struct runtime_env *env,
	struct configuration *config)
{
	struct timespec timeout;
	const int eventlist_size = 1;
	struct kevent eventlist[eventlist_size];
	int nevents, i;

	TRACE_MSG("=> processing_loop");
	memset(&timeout, 0, sizeof(struct timespec));
	memset(&eventlist, 0, sizeof(struct kevent) * eventlist_size);

	for (;;) {
		nevents = kevent(env->queue, NULL, 0, eventlist,
	    		eventlist_size, NULL);
		/*
		 * we can only receive 1 event on success
		 */
		if (nevents == 1) {
			struct kevent *event_data;
			event_data = &eventlist[0];

			if ((int)event_data->ident == env->sockfd) {
				for (i = 0; i < event_data->data; ++i)
				    accept_connection(event_data, env, config);

				EV_SET(eventlist, s_runtime_env->sockfd,
				    EVFILT_READ, EV_ADD | EV_ONESHOT,
				    0, 0, 0);
				memset(&timeout, 0,
				    sizeof(struct timespec));
				kevent(s_runtime_env->queue, eventlist,
				    1, NULL, 0, &timeout);

			} else {
				switch (event_data->filter) {
				case EVFILT_READ:
				case EVFILT_WRITE:
					process_socket_event(event_data,
						env, config);
					break;
				case EVFILT_TIMER:
					process_timer_event(event_data,
						env, config);
					break;
				default:
					break;
				}
			}
		} else {
			/* this branch shouldn't be currently executed */
		}
	}

	TRACE_MSG("<= processing_loop");
}

/*
 * Wrapper above the processing loop function. It sets the thread signal mask
 * to avoid SIGPIPE signals (which can happen if the client works incorrectly).
 */
static void *
processing_thread(void *data)
{
	struct processing_thread_args	*args;
	sigset_t new;

	TRACE_MSG("=> processing_thread");
	args = (struct processing_thread_args *)data;

	sigemptyset(&new);
	sigaddset(&new, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &new, NULL) != 0)
		LOG_ERR_1("processing thread",
			"thread can't block the SIGPIPE signal");

	processing_loop(args->the_cache, args->the_runtime_env,
		args->the_configuration);
	free(args);
	TRACE_MSG("<= processing_thread");

	return (NULL);
}

void
get_time_func(struct timeval *time)
{
	struct timespec res;
	memset(&res, 0, sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, &res);

	time->tv_sec = res.tv_sec;
	time->tv_usec = 0;
}

/*
 * The idea of _nss_cache_cycle_prevention_function is that nsdispatch
 * will search for this symbol in the executable. This symbol is the
 * attribute of the caching daemon. So, if it exists, nsdispatch won't try
 * to connect to the caching daemon and will just ignore the 'cache'
 * source in the nsswitch.conf. This method helps to avoid cycles and
 * organize self-performing requests.
 *
 * (not actually a function; it used to be, but it doesn't make any
 * difference, as long as it has external linkage)
 */
void *_nss_cache_cycle_prevention_function;

int
main(int argc, char *argv[])
{
	struct processing_thread_args *thread_args;
	pthread_t *threads;

	struct pidfh *pidfile;
	pid_t pid;

	char const *config_file;
	char const *error_str;
	int error_line;
	int i, res;

	int trace_mode_enabled;
	int force_single_threaded;
	int do_not_daemonize;
	int clear_user_cache_entries, clear_all_cache_entries;
	char *user_config_entry_name, *global_config_entry_name;
	int show_statistics;
	int daemon_mode, interactive_mode;


	/* by default all debug messages are omitted */
	TRACE_OFF();

	/* parsing command line arguments */
	trace_mode_enabled = 0;
	force_single_threaded = 0;
	do_not_daemonize = 0;
	clear_user_cache_entries = 0;
	clear_all_cache_entries = 0;
	show_statistics = 0;
	user_config_entry_name = NULL;
	global_config_entry_name = NULL;
	while ((res = getopt(argc, argv, "nstdi:I:")) != -1) {
		switch (res) {
		case 'n':
			do_not_daemonize = 1;
			break;
		case 's':
			force_single_threaded = 1;
			break;
		case 't':
			trace_mode_enabled = 1;
			break;
		case 'i':
			clear_user_cache_entries = 1;
			if (optarg != NULL)
				if (strcmp(optarg, "all") != 0)
					user_config_entry_name = strdup(optarg);
			break;
		case 'I':
			clear_all_cache_entries = 1;
			if (optarg != NULL)
				if (strcmp(optarg, "all") != 0)
					global_config_entry_name =
						strdup(optarg);
			break;
		case 'd':
			show_statistics = 1;
			break;
		case '?':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	daemon_mode = do_not_daemonize | force_single_threaded |
		trace_mode_enabled;
	interactive_mode = clear_user_cache_entries | clear_all_cache_entries |
		show_statistics;

	if ((daemon_mode != 0) && (interactive_mode != 0)) {
		LOG_ERR_1("main", "daemon mode and interactive_mode arguments "
			"can't be used together");
		usage();
	}

	if (interactive_mode != 0) {
		FILE *pidfin = fopen(DEFAULT_PIDFILE_PATH, "r");
		char pidbuf[256];

		struct nscd_connection_params connection_params;
		nscd_connection connection;

		int result;

		if (pidfin == NULL)
			errx(EXIT_FAILURE, "There is no daemon running.");

		memset(pidbuf, 0, sizeof(pidbuf));
		fread(pidbuf, sizeof(pidbuf) - 1, 1, pidfin);
		fclose(pidfin);

		if (ferror(pidfin) != 0)
			errx(EXIT_FAILURE, "Can't read from pidfile.");

		if (sscanf(pidbuf, "%d", &pid) != 1)
			errx(EXIT_FAILURE, "Invalid pidfile.");
		LOG_MSG_1("main", "daemon PID is %d", pid);


		memset(&connection_params, 0,
			sizeof(struct nscd_connection_params));
		connection_params.socket_path = DEFAULT_SOCKET_PATH;
		connection = open_nscd_connection__(&connection_params);
		if (connection == INVALID_NSCD_CONNECTION)
			errx(EXIT_FAILURE, "Can't connect to the daemon.");

		if (clear_user_cache_entries != 0) {
			result = nscd_transform__(connection,
				user_config_entry_name, TT_USER);
			if (result != 0)
				LOG_MSG_1("main",
					"user cache transformation failed");
			else
				LOG_MSG_1("main",
					"user cache_transformation "
					"succeeded");
		}

		if (clear_all_cache_entries != 0) {
			if (geteuid() != 0)
				errx(EXIT_FAILURE, "Only root can initiate "
					"global cache transformation.");

			result = nscd_transform__(connection,
				global_config_entry_name, TT_ALL);
			if (result != 0)
				LOG_MSG_1("main",
					"global cache transformation "
					"failed");
			else
				LOG_MSG_1("main",
					"global cache transformation "
					"succeeded");
		}

		close_nscd_connection__(connection);

		free(user_config_entry_name);
		free(global_config_entry_name);
		return (EXIT_SUCCESS);
	}

	pidfile = pidfile_open(DEFAULT_PIDFILE_PATH, 0644, &pid);
	if (pidfile == NULL) {
		if (errno == EEXIST)
			errx(EXIT_FAILURE, "Daemon already running, pid: %d.",
				pid);
		warn("Cannot open or create pidfile");
	}

	if (trace_mode_enabled == 1)
		TRACE_ON();

	/* blocking the main thread from receiving SIGPIPE signal */
	sigblock(sigmask(SIGPIPE));

	/* daemonization */
	if (do_not_daemonize == 0) {
		res = daemon(0, trace_mode_enabled == 0 ? 0 : 1);
		if (res != 0) {
			LOG_ERR_1("main", "can't daemonize myself: %s",
		    		strerror(errno));
			pidfile_remove(pidfile);
			goto fin;
		} else
			LOG_MSG_1("main", "successfully daemonized");
	}

	pidfile_write(pidfile);

	s_agent_table = init_agent_table();
	register_agent(s_agent_table, init_passwd_agent());
	register_agent(s_agent_table, init_passwd_mp_agent());
	register_agent(s_agent_table, init_group_agent());
	register_agent(s_agent_table, init_group_mp_agent());
	register_agent(s_agent_table, init_services_agent());
	register_agent(s_agent_table, init_services_mp_agent());
	LOG_MSG_1("main", "request agents registered successfully");

	/*
 	 * Hosts agent can't work properly until we have access to the
	 * appropriate dtab structures, which are used in nsdispatch
	 * calls
	 *
	 register_agent(s_agent_table, init_hosts_agent());
	*/

	/* configuration initialization */
	s_configuration = init_configuration();
	fill_configuration_defaults(s_configuration);

	error_str = NULL;
	error_line = 0;
	config_file = CONFIG_PATH;

	res = parse_config_file(s_configuration, config_file, &error_str,
		&error_line);
	if ((res != 0) && (error_str == NULL)) {
		config_file = DEFAULT_CONFIG_PATH;
		res = parse_config_file(s_configuration, config_file,
			&error_str, &error_line);
	}

	if (res != 0) {
		if (error_str != NULL) {
		LOG_ERR_1("main", "error in configuration file(%s, %d): %s\n",
			config_file, error_line, error_str);
		} else {
		LOG_ERR_1("main", "no configuration file found "
		    	"- was looking for %s and %s",
			CONFIG_PATH, DEFAULT_CONFIG_PATH);
		}
		destroy_configuration(s_configuration);
		return (-1);
	}

	if (force_single_threaded == 1)
		s_configuration->threads_num = 1;

	/* cache initialization */
	s_cache = init_cache_(s_configuration);
	if (s_cache == NULL) {
		LOG_ERR_1("main", "can't initialize the cache");
		destroy_configuration(s_configuration);
		return (-1);
	}

	/* runtime environment initialization */
	s_runtime_env = init_runtime_env(s_configuration);
	if (s_runtime_env == NULL) {
		LOG_ERR_1("main", "can't initialize the runtime environment");
		destroy_configuration(s_configuration);
		destroy_cache_(s_cache);
		return (-1);
	}

	if (s_configuration->threads_num > 1) {
		threads = calloc(s_configuration->threads_num,
			sizeof(*threads));
		for (i = 0; i < s_configuration->threads_num; ++i) {
			thread_args = malloc(
				sizeof(*thread_args));
			thread_args->the_cache = s_cache;
			thread_args->the_runtime_env = s_runtime_env;
			thread_args->the_configuration = s_configuration;

			LOG_MSG_1("main", "thread #%d was successfully created",
				i);
			pthread_create(&threads[i], NULL, processing_thread,
				thread_args);

			thread_args = NULL;
		}

		for (i = 0; i < s_configuration->threads_num; ++i)
			pthread_join(threads[i], NULL);
	} else {
		LOG_MSG_1("main", "working in single-threaded mode");
		processing_loop(s_cache, s_runtime_env, s_configuration);
	}

fin:
	/* runtime environment destruction */
	destroy_runtime_env(s_runtime_env);

	/* cache destruction */
	destroy_cache_(s_cache);

	/* configuration destruction */
	destroy_configuration(s_configuration);

	/* agents table destruction */
	destroy_agent_table(s_agent_table);

	pidfile_remove(pidfile);
	return (EXIT_SUCCESS);
}
