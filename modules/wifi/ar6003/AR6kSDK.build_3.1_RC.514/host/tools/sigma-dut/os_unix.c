/*
 * OS specific functions for UNIX/POSIX systems
 * Copyright (c) 2005-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#ifdef ANDROID
#include <linux/capability.h>
#include <linux/prctl.h>
#include <private/android_filesystem_config.h>
#endif /* ANDROID */

#include "os.h"

#ifdef WPA_TRACE

#include "common.h"
#include "list.h"
#include "wpa_debug.h"
#include "trace.h"

static struct dl_list alloc_list;

#define ALLOC_MAGIC 0xa84ef1b2
#define FREED_MAGIC 0x67fd487a

struct os_alloc_trace {
	unsigned int magic;
	struct dl_list list;
	size_t len;
	WPA_TRACE_INFO
};

#endif /* WPA_TRACE */


void os_sleep(os_time_t sec, os_time_t usec)
{
	if (sec)
		sleep(sec);
	if (usec)
		usleep(usec);
}


int os_get_time(struct os_time *t)
{
	int res;
	struct timeval tv;
	res = gettimeofday(&tv, NULL);
	t->sec = tv.tv_sec;
	t->usec = tv.tv_usec;
	return res;
}


int os_mktime(int year, int month, int day, int hour, int min, int sec,
	      os_time_t *t)
{
	struct tm tm, *tm1;
	time_t t_local, t1, t2;
	os_time_t tz_offset;

	if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
	    hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 ||
	    sec > 60)
		return -1;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;

	t_local = mktime(&tm);

	/* figure out offset to UTC */
	tm1 = localtime(&t_local);
	if (tm1) {
		t1 = mktime(tm1);
		tm1 = gmtime(&t_local);
		if (tm1) {
			t2 = mktime(tm1);
			tz_offset = t2 - t1;
		} else
			tz_offset = 0;
	} else
		tz_offset = 0;

	*t = (os_time_t) t_local - tz_offset;
	return 0;
}


#ifdef __APPLE__
#include <fcntl.h>
static int os_daemon(int nochdir, int noclose)
{
	int devnull;

	if (chdir("/") < 0)
		return -1;

	devnull = open("/dev/null", O_RDWR);
	if (devnull < 0)
		return -1;

	if (dup2(devnull, STDIN_FILENO) < 0) {
		close(devnull);
		return -1;
	}

	if (dup2(devnull, STDOUT_FILENO) < 0) {
		close(devnull);
		return -1;
	}

	if (dup2(devnull, STDERR_FILENO) < 0) {
		close(devnull);
		return -1;
	}

	return 0;
}
#else /* __APPLE__ */
#define os_daemon daemon
#endif /* __APPLE__ */


int os_daemonize(const char *pid_file)
{
#if defined(__uClinux__) || defined(__sun__)
	return -1;
#else /* defined(__uClinux__) || defined(__sun__) */
	if (os_daemon(0, 0)) {
		perror("daemon");
		return -1;
	}

	if (pid_file) {
		FILE *f = fopen(pid_file, "w");
		if (f) {
			fprintf(f, "%u\n", getpid());
			fclose(f);
		}
	}

	return -0;
#endif /* defined(__uClinux__) || defined(__sun__) */
}


void os_daemonize_terminate(const char *pid_file)
{
	if (pid_file)
		unlink(pid_file);
}


int os_get_random(unsigned char *buf, size_t len)
{
	FILE *f;
	size_t rc;

	f = fopen("/dev/urandom", "rb");
	if (f == NULL) {
		printf("Could not open /dev/urandom.\n");
		return -1;
	}

	rc = fread(buf, 1, len, f);
	fclose(f);

	return rc != len ? -1 : 0;
}


unsigned long os_random(void)
{
	return random();
}


char * os_rel2abs_path(const char *rel_path)
{
	char *buf = NULL, *cwd, *ret;
	size_t len = 128, cwd_len, rel_len, ret_len;
	int last_errno;

	if (rel_path[0] == '/')
		return os_strdup(rel_path);

	for (;;) {
		buf = os_malloc(len);
		if (buf == NULL)
			return NULL;
		cwd = getcwd(buf, len);
		if (cwd == NULL) {
			last_errno = errno;
			os_free(buf);
			if (last_errno != ERANGE)
				return NULL;
			len *= 2;
			if (len > 2000)
				return NULL;
		} else {
			buf[len - 1] = '\0';
			break;
		}
	}

	cwd_len = os_strlen(cwd);
	rel_len = os_strlen(rel_path);
	ret_len = cwd_len + 1 + rel_len + 1;
	ret = os_malloc(ret_len);
	if (ret) {
		os_memcpy(ret, cwd, cwd_len);
		ret[cwd_len] = '/';
		os_memcpy(ret + cwd_len + 1, rel_path, rel_len);
		ret[ret_len - 1] = '\0';
	}
	os_free(buf);
	return ret;
}


int os_program_init(void)
{
#ifdef ANDROID
	/*
	 * We ignore errors here since errors are normal if we
	 * are already running as non-root.
	 */
	gid_t groups[] = { AID_INET, AID_WIFI, AID_KEYSTORE };
	struct __user_cap_header_struct header;
	struct __user_cap_data_struct cap;

	setgroups(sizeof(groups)/sizeof(groups[0]), groups);

	prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);

	setgid(AID_WIFI);
	setuid(AID_WIFI);

	header.version = _LINUX_CAPABILITY_VERSION;
	header.pid = 0;
	cap.effective = cap.permitted =
		(1 << CAP_NET_ADMIN) | (1 << CAP_NET_RAW);
	cap.inheritable = 0;
	capset(&header, &cap);
#endif /* ANDROID */

#ifdef WPA_TRACE
	dl_list_init(&alloc_list);
#endif /* WPA_TRACE */
	return 0;
}


void os_program_deinit(void)
{
#ifdef WPA_TRACE
	struct os_alloc_trace *a;
	unsigned long total = 0;
	dl_list_for_each(a, &alloc_list, struct os_alloc_trace, list) {
		total += a->len;
		if (a->magic != ALLOC_MAGIC) {
			wpa_printf(MSG_INFO, "MEMLEAK[%p]: invalid magic 0x%x "
				   "len %lu",
				   a, a->magic, (unsigned long) a->len);
			continue;
		}
		wpa_printf(MSG_INFO, "MEMLEAK[%p]: len %lu",
			   a, (unsigned long) a->len);
		wpa_trace_dump("memleak", a);
	}
	if (total)
		wpa_printf(MSG_INFO, "MEMLEAK: total %lu bytes",
			   (unsigned long) total);
#endif /* WPA_TRACE */
}


int os_setenv(const char *name, const char *value, int overwrite)
{
	return setenv(name, value, overwrite);
}


int os_unsetenv(const char *name)
{
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__) || \
    defined(__OpenBSD__)
	unsetenv(name);
	return 0;
#else
	return unsetenv(name);
#endif
}


char * os_readfile(const char *name, size_t *len)
{
	FILE *f;
	char *buf;
	long pos;

	f = fopen(name, "rb");
	if (f == NULL)
		return NULL;

	if (fseek(f, 0, SEEK_END) < 0 || (pos = ftell(f)) < 0) {
		fclose(f);
		return NULL;
	}
	*len = pos;
	if (fseek(f, 0, SEEK_SET) < 0) {
		fclose(f);
		return NULL;
	}

	buf = os_malloc(*len);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}

	if (fread(buf, 1, *len, f) != *len) {
		fclose(f);
		os_free(buf);
		return NULL;
	}

	fclose(f);

	return buf;
}


#ifndef WPA_TRACE
void * os_zalloc(size_t size)
{
	return calloc(1, size);
}
#endif /* WPA_TRACE */


size_t os_strlcpy(char *dest, const char *src, size_t siz)
{
	const char *s = src;
	size_t left = siz;

	if (left) {
		/* Copy string up to the maximum size of the dest buffer */
		while (--left != 0) {
			if ((*dest++ = *s++) == '\0')
				break;
		}
	}

	if (left == 0) {
		/* Not enough room for the string; force NUL-termination */
		if (siz != 0)
			*dest = '\0';
		while (*s++)
			; /* determine total src string length */
	}

	return s - src - 1;
}


#ifdef WPA_TRACE

void * os_malloc(size_t size)
{
	struct os_alloc_trace *a;
	a = malloc(sizeof(*a) + size);
	if (a == NULL)
		return NULL;
	a->magic = ALLOC_MAGIC;
	dl_list_add(&alloc_list, &a->list);
	a->len = size;
	wpa_trace_record(a);
	return a + 1;
}


void * os_realloc(void *ptr, size_t size)
{
	struct os_alloc_trace *a;
	size_t copy_len;
	void *n;

	if (ptr == NULL)
		return os_malloc(size);

	a = (struct os_alloc_trace *) ptr - 1;
	if (a->magic != ALLOC_MAGIC) {
		wpa_printf(MSG_INFO, "REALLOC[%p]: invalid magic 0x%x%s",
			   a, a->magic,
			   a->magic == FREED_MAGIC ? " (already freed)" : "");
		wpa_trace_show("Invalid os_realloc() call");
		abort();
	}
	n = os_malloc(size);
	if (n == NULL)
		return NULL;
	copy_len = a->len;
	if (copy_len > size)
		copy_len = size;
	os_memcpy(n, a + 1, copy_len);
	os_free(ptr);
	return n;
}


void os_free(void *ptr)
{
	struct os_alloc_trace *a;

	if (ptr == NULL)
		return;
	a = (struct os_alloc_trace *) ptr - 1;
	if (a->magic != ALLOC_MAGIC) {
		wpa_printf(MSG_INFO, "FREE[%p]: invalid magic 0x%x%s",
			   a, a->magic,
			   a->magic == FREED_MAGIC ? " (already freed)" : "");
		wpa_trace_show("Invalid os_free() call");
		abort();
	}
	dl_list_del(&a->list);
	a->magic = FREED_MAGIC;

	wpa_trace_check_ref(ptr);
	free(a);
}


void * os_zalloc(size_t size)
{
	void *ptr = os_malloc(size);
	if (ptr)
		os_memset(ptr, 0, size);
	return ptr;
}


char * os_strdup(const char *s)
{
	size_t len;
	char *d;
	len = os_strlen(s);
	d = os_malloc(len + 1);
	if (d == NULL)
		return NULL;
	os_memcpy(d, s, len);
	d[len] = '\0';
	return d;
}

#endif /* WPA_TRACE */
