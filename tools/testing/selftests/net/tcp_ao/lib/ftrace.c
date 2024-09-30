// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <unistd.h>
#include "../../../../../include/linux/kernel.h"
#include "aolib.h"

static char ftrace_path[] = "ksft-ftrace-XXXXXX";
static bool ftrace_mounted;
uint64_t ns_cookie1, ns_cookie2;

struct test_ftracer {
	pthread_t tracer_thread;
	int	error;
	char	*instance_path;
	FILE	*trace_pipe;

	enum ftracer_op (*process_line)(const char *line);
	void (*destructor)(struct test_ftracer *tracer);
	bool (*expecting_more)(void);

	char	**saved_lines;
	size_t	saved_lines_size;
	size_t	next_line_ind;

	pthread_cond_t met_all_expected;
	pthread_mutex_t met_all_expected_lock;

	struct test_ftracer *next;
};

static struct test_ftracer *ftracers;
static pthread_mutex_t ftracers_lock = PTHREAD_MUTEX_INITIALIZER;

static int mount_ftrace(void)
{
	if (!mkdtemp(ftrace_path))
		test_error("Can't create temp dir");

	if (mount("tracefs", ftrace_path, "tracefs", 0, "rw"))
		return -errno;

	ftrace_mounted = true;

	return 0;
}

static void unmount_ftrace(void)
{
	if (ftrace_mounted && umount(ftrace_path))
		test_print("Failed on cleanup: can't unmount tracefs: %m");

	if (rmdir(ftrace_path))
		test_error("Failed on cleanup: can't remove ftrace dir %s",
			   ftrace_path);
}

struct opts_list_t {
	char *opt_name;
	struct opts_list_t *next;
};

static int disable_trace_options(const char *ftrace_path)
{
	struct opts_list_t *opts_list = NULL;
	char *fopts, *line = NULL;
	size_t buf_len = 0;
	ssize_t line_len;
	int ret = 0;
	FILE *opts;

	fopts = test_sprintf("%s/%s", ftrace_path, "trace_options");
	if (!fopts)
		return -ENOMEM;

	opts = fopen(fopts, "r+");
	if (!opts) {
		ret = -errno;
		goto out_free;
	}

	while ((line_len = getline(&line, &buf_len, opts)) != -1) {
		struct opts_list_t *tmp;

		if (!strncmp(line, "no", 2))
			continue;

		tmp = malloc(sizeof(*tmp));
		if (!tmp) {
			ret = -ENOMEM;
			goto out_free_opts_list;
		}
		tmp->next = opts_list;
		tmp->opt_name = test_sprintf("no%s", line);
		if (!tmp->opt_name) {
			ret = -ENOMEM;
			free(tmp);
			goto out_free_opts_list;
		}
		opts_list = tmp;
	}

	while (opts_list) {
		struct opts_list_t *tmp = opts_list;

		fseek(opts, 0, SEEK_SET);
		fwrite(tmp->opt_name, 1, strlen(tmp->opt_name), opts);

		opts_list = opts_list->next;
		free(tmp->opt_name);
		free(tmp);
	}

out_free_opts_list:
	while (opts_list) {
		struct opts_list_t *tmp = opts_list;

		opts_list = opts_list->next;
		free(tmp->opt_name);
		free(tmp);
	}
	free(line);
	fclose(opts);
out_free:
	free(fopts);
	return ret;
}

static int setup_buffer_size(const char *ftrace_path, size_t sz)
{
	char *fbuf_size = test_sprintf("%s/buffer_size_kb", ftrace_path);
	int ret;

	if (!fbuf_size)
		return -1;

	ret = test_echo(fbuf_size, 0, "%zu", sz);
	free(fbuf_size);
	return ret;
}

static int setup_ftrace_instance(struct test_ftracer *tracer, const char *name)
{
	char *tmp;

	tmp = test_sprintf("%s/instances/ksft-%s-XXXXXX", ftrace_path, name);
	if (!tmp)
		return -ENOMEM;

	tracer->instance_path = mkdtemp(tmp);
	if (!tracer->instance_path) {
		free(tmp);
		return -errno;
	}

	return 0;
}

static void remove_ftrace_instance(struct test_ftracer *tracer)
{
	if (rmdir(tracer->instance_path))
		test_print("Failed on cleanup: can't remove ftrace instance %s",
			   tracer->instance_path);
	free(tracer->instance_path);
}

static void tracer_cleanup(void *arg)
{
	struct test_ftracer *tracer = arg;

	fclose(tracer->trace_pipe);
}

static void tracer_set_error(struct test_ftracer *tracer, int error)
{
	if (!tracer->error)
		tracer->error = error;
}

const size_t tracer_get_savedlines_nr(struct test_ftracer *tracer)
{
	return tracer->next_line_ind;
}

const char **tracer_get_savedlines(struct test_ftracer *tracer)
{
	return (const char **)tracer->saved_lines;
}

static void *tracer_thread_func(void *arg)
{
	struct test_ftracer *tracer = arg;

	pthread_cleanup_push(tracer_cleanup, arg);

	while (tracer->next_line_ind < tracer->saved_lines_size) {
		char **lp = &tracer->saved_lines[tracer->next_line_ind];
		enum ftracer_op op;
		size_t buf_len = 0;
		ssize_t line_len;

		line_len = getline(lp, &buf_len, tracer->trace_pipe);
		if (line_len == -1)
			break;

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		op = tracer->process_line(*lp);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		if (tracer->expecting_more) {
			pthread_mutex_lock(&tracer->met_all_expected_lock);
			if (!tracer->expecting_more())
				pthread_cond_signal(&tracer->met_all_expected);
			pthread_mutex_unlock(&tracer->met_all_expected_lock);
		}

		if (op == FTRACER_LINE_DISCARD)
			continue;
		if (op == FTRACER_EXIT)
			break;
		if (op != FTRACER_LINE_PRESERVE)
			test_error("unexpected tracer command %d", op);

		tracer->next_line_ind++;
		buf_len = 0;
	}
	test_print("too many lines in ftracer buffer %zu, exiting tracer",
		   tracer->next_line_ind);

	pthread_cleanup_pop(1);
	return NULL;
}

static int setup_trace_thread(struct test_ftracer *tracer)
{
	int ret = 0;
	char *path;

	path = test_sprintf("%s/trace_pipe", tracer->instance_path);
	if (!path)
		return -ENOMEM;

	tracer->trace_pipe = fopen(path, "r");
	if (!tracer->trace_pipe) {
		ret = -errno;
		goto out_free;
	}

	if (pthread_create(&tracer->tracer_thread, NULL,
			   tracer_thread_func, (void *)tracer)) {
		ret = -errno;
		fclose(tracer->trace_pipe);
	}

out_free:
	free(path);
	return ret;
}

static void stop_trace_thread(struct test_ftracer *tracer)
{
	void *res;

	if (pthread_cancel(tracer->tracer_thread)) {
		test_print("Can't stop tracer pthread: %m");
		tracer_set_error(tracer, -errno);
	}
	if (pthread_join(tracer->tracer_thread, &res)) {
		test_print("Can't join tracer pthread: %m");
		tracer_set_error(tracer, -errno);
	}
	if (res != PTHREAD_CANCELED) {
		test_print("Tracer thread wasn't canceled");
		tracer_set_error(tracer, -errno);
	}
	if (tracer->error)
		test_fail("tracer errored by %s", strerror(tracer->error));
}

static void final_wait_for_events(struct test_ftracer *tracer,
				  unsigned timeout_sec)
{
	struct timespec timeout;
	struct timeval now;
	int ret = 0;

	if (!tracer->expecting_more)
		return;

	pthread_mutex_lock(&tracer->met_all_expected_lock);
	gettimeofday(&now, NULL);
	timeout.tv_sec = now.tv_sec + timeout_sec;
	timeout.tv_nsec = now.tv_usec * 1000;

	while (tracer->expecting_more() && ret != ETIMEDOUT)
		ret = pthread_cond_timedwait(&tracer->met_all_expected,
				&tracer->met_all_expected_lock, &timeout);
	pthread_mutex_unlock(&tracer->met_all_expected_lock);
}

int setup_trace_event(struct test_ftracer *tracer,
		      const char *event, const char *filter)
{
	char *enable_path, *filter_path, *instance = tracer->instance_path;
	int ret;

	enable_path = test_sprintf("%s/events/%s/enable", instance, event);
	if (!enable_path)
		return -ENOMEM;

	filter_path = test_sprintf("%s/events/%s/filter", instance, event);
	if (!filter_path) {
		ret = -ENOMEM;
		goto out_free;
	}

	ret = test_echo(filter_path, 0, "%s", filter);
	if (!ret)
		ret = test_echo(enable_path, 0, "1");

out_free:
	free(filter_path);
	free(enable_path);
	return ret;
}

struct test_ftracer *create_ftracer(const char *name,
				    enum ftracer_op (*process_line)(const char *line),
				    void (*destructor)(struct test_ftracer *tracer),
				    bool (*expecting_more)(void),
				    size_t lines_buf_sz, size_t buffer_size_kb)
{
	struct test_ftracer *tracer;
	int err;

	/* XXX: separate __create_ftracer() helper and do here
	 * if (!kernel_config_has(KCONFIG_FTRACE))
	 *	return NULL;
	 */

	tracer = malloc(sizeof(*tracer));
	if (!tracer) {
		test_print("malloc()");
		return NULL;
	}

	memset(tracer, 0, sizeof(*tracer));

	err = setup_ftrace_instance(tracer, name);
	if (err) {
		test_print("setup_ftrace_instance(): %d", err);
		goto err_free;
	}

	err = disable_trace_options(tracer->instance_path);
	if (err) {
		test_print("disable_trace_options(): %d", err);
		goto err_remove;
	}

	err = setup_buffer_size(tracer->instance_path, buffer_size_kb);
	if (err) {
		test_print("disable_trace_options(): %d", err);
		goto err_remove;
	}

	tracer->saved_lines = calloc(lines_buf_sz, sizeof(tracer->saved_lines[0]));
	if (!tracer->saved_lines) {
		test_print("calloc()");
		goto err_remove;
	}
	tracer->saved_lines_size = lines_buf_sz;

	tracer->process_line	= process_line;
	tracer->destructor	= destructor;
	tracer->expecting_more	= expecting_more;

	err = pthread_cond_init(&tracer->met_all_expected, NULL);
	if (err) {
		test_print("pthread_cond_init(): %d", err);
		goto err_free_lines;
	}

	err = pthread_mutex_init(&tracer->met_all_expected_lock, NULL);
	if (err) {
		test_print("pthread_mutex_init(): %d", err);
		goto err_cond_destroy;
	}

	err = setup_trace_thread(tracer);
	if (err) {
		test_print("setup_trace_thread(): %d", err);
		goto err_mutex_destroy;
	}

	pthread_mutex_lock(&ftracers_lock);
	tracer->next = ftracers;
	ftracers = tracer;
	pthread_mutex_unlock(&ftracers_lock);

	return tracer;

err_mutex_destroy:
	pthread_mutex_destroy(&tracer->met_all_expected_lock);
err_cond_destroy:
	pthread_cond_destroy(&tracer->met_all_expected);
err_free_lines:
	free(tracer->saved_lines);
err_remove:
	remove_ftrace_instance(tracer);
err_free:
	free(tracer);
	return NULL;
}

static void __destroy_ftracer(struct test_ftracer *tracer)
{
	size_t i;

	final_wait_for_events(tracer, TEST_TIMEOUT_SEC);
	stop_trace_thread(tracer);
	remove_ftrace_instance(tracer);
	if (tracer->destructor)
		tracer->destructor(tracer);
	for (i = 0; i < tracer->saved_lines_size; i++)
		free(tracer->saved_lines[i]);
	pthread_cond_destroy(&tracer->met_all_expected);
	pthread_mutex_destroy(&tracer->met_all_expected_lock);
	free(tracer);
}

void destroy_ftracer(struct test_ftracer *tracer)
{
	pthread_mutex_lock(&ftracers_lock);
	if (tracer == ftracers) {
		ftracers = tracer->next;
	} else {
		struct test_ftracer *f = ftracers;

		while (f->next != tracer) {
			if (!f->next)
				test_error("tracers list corruption or double free %p", tracer);
			f = f->next;
		}
		f->next = tracer->next;
	}
	tracer->next = NULL;
	pthread_mutex_unlock(&ftracers_lock);
	__destroy_ftracer(tracer);
}

static void destroy_all_ftracers(void)
{
	struct test_ftracer *f;

	pthread_mutex_lock(&ftracers_lock);
	f = ftracers;
	ftracers = NULL;
	pthread_mutex_unlock(&ftracers_lock);

	while (f) {
		struct test_ftracer *n = f->next;

		f->next = NULL;
		__destroy_ftracer(f);
		f = n;
	}
}

static void test_unset_tracing(void)
{
	destroy_all_ftracers();
	unmount_ftrace();
}

int test_setup_tracing(void)
{
	/*
	 * Just a basic protection - this should be called only once from
	 * lib/kconfig. Not thread safe, which is fine as it's early, before
	 * threads are created.
	 */
	static int already_set;
	int err;

	if (already_set)
		return -1;

	/* Needs net-namespace cookies for filters */
	if (ns_cookie1 == ns_cookie2) {
		test_print("net-namespace cookies: %" PRIu64 " == %" PRIu64 ", can't set up tracing",
			   ns_cookie1, ns_cookie2);
		return -1;
	}

	already_set = 1;

	test_add_destructor(test_unset_tracing);

	err = mount_ftrace();
	if (err) {
		test_print("failed to mount_ftrace(): %d", err);
		return err;
	}

	return setup_aolib_ftracer();
}

static int get_ns_cookie(int nsfd, uint64_t *out)
{
	int old_ns = switch_save_ns(nsfd);
	socklen_t size = sizeof(*out);
	int sk;

	sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0) {
		test_print("socket(): %m");
		return -errno;
	}

	if (getsockopt(sk, SOL_SOCKET, SO_NETNS_COOKIE, out, &size)) {
		test_print("getsockopt(SO_NETNS_COOKIE): %m");
		close(sk);
		return -errno;
	}

	close(sk);
	switch_close_ns(old_ns);
	return 0;
}

void test_init_ftrace(int nsfd1, int nsfd2)
{
	get_ns_cookie(nsfd1, &ns_cookie1);
	get_ns_cookie(nsfd2, &ns_cookie2);
	/* Populate kernel config state */
	kernel_config_has(KCONFIG_FTRACE);
}
