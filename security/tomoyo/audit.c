/*
 * security/tomoyo/audit.c
 *
 * Pathname restriction functions.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 */

#include "common.h"
#include <linux/slab.h>

/**
 * tomoyo_convert_time - Convert time_t to YYYY/MM/DD hh/mm/ss.
 *
 * @time:  Seconds since 1970/01/01 00:00:00.
 * @stamp: Pointer to "struct tomoyo_time".
 *
 * Returns nothing.
 *
 * This function does not handle Y2038 problem.
 */
static void tomoyo_convert_time(time_t time, struct tomoyo_time *stamp)
{
	static const u16 tomoyo_eom[2][12] = {
		{ 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
		{ 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
	};
	u16 y;
	u8 m;
	bool r;
	stamp->sec = time % 60;
	time /= 60;
	stamp->min = time % 60;
	time /= 60;
	stamp->hour = time % 24;
	time /= 24;
	for (y = 1970; ; y++) {
		const unsigned short days = (y & 3) ? 365 : 366;
		if (time < days)
			break;
		time -= days;
	}
	r = (y & 3) == 0;
	for (m = 0; m < 11 && time >= tomoyo_eom[r][m]; m++)
		;
	if (m)
		time -= tomoyo_eom[r][m - 1];
	stamp->year = y;
	stamp->month = ++m;
	stamp->day = ++time;
}

/**
 * tomoyo_print_header - Get header line of audit log.
 *
 * @r: Pointer to "struct tomoyo_request_info".
 *
 * Returns string representation.
 *
 * This function uses kmalloc(), so caller must kfree() if this function
 * didn't return NULL.
 */
static char *tomoyo_print_header(struct tomoyo_request_info *r)
{
	struct tomoyo_time stamp;
	const pid_t gpid = task_pid_nr(current);
	static const int tomoyo_buffer_len = 4096;
	char *buffer = kmalloc(tomoyo_buffer_len, GFP_NOFS);
	pid_t ppid;
	if (!buffer)
		return NULL;
	{
		struct timeval tv;
		do_gettimeofday(&tv);
		tomoyo_convert_time(tv.tv_sec, &stamp);
	}
	rcu_read_lock();
	ppid = task_tgid_vnr(current->real_parent);
	rcu_read_unlock();
	snprintf(buffer, tomoyo_buffer_len - 1,
		 "#%04u/%02u/%02u %02u:%02u:%02u# profile=%u mode=%s "
		 "granted=%s (global-pid=%u) task={ pid=%u ppid=%u "
		 "uid=%u gid=%u euid=%u egid=%u suid=%u sgid=%u "
		 "fsuid=%u fsgid=%u }",
		 stamp.year, stamp.month, stamp.day, stamp.hour,
		 stamp.min, stamp.sec, r->profile, tomoyo_mode[r->mode],
		 tomoyo_yesno(r->granted), gpid, task_tgid_vnr(current), ppid,
		 current_uid(), current_gid(), current_euid(), current_egid(),
		 current_suid(), current_sgid(), current_fsuid(),
		 current_fsgid());
	return buffer;
}

/**
 * tomoyo_init_log - Allocate buffer for audit logs.
 *
 * @r:    Pointer to "struct tomoyo_request_info".
 * @len:  Buffer size needed for @fmt and @args.
 * @fmt:  The printf()'s format string.
 * @args: va_list structure for @fmt.
 *
 * Returns pointer to allocated memory.
 *
 * This function uses kzalloc(), so caller must kfree() if this function
 * didn't return NULL.
 */
char *tomoyo_init_log(struct tomoyo_request_info *r, int len, const char *fmt,
		      va_list args)
{
	char *buf = NULL;
	const char *header = NULL;
	int pos;
	const char *domainname = tomoyo_domain()->domainname->name;
	header = tomoyo_print_header(r);
	if (!header)
		return NULL;
	/* +10 is for '\n' etc. and '\0'. */
	len += strlen(domainname) + strlen(header) + 10;
	len = tomoyo_round2(len);
	buf = kzalloc(len, GFP_NOFS);
	if (!buf)
		goto out;
	len--;
	pos = snprintf(buf, len, "%s", header);
	pos += snprintf(buf + pos, len - pos, "\n%s\n", domainname);
	vsnprintf(buf + pos, len - pos, fmt, args);
out:
	kfree(header);
	return buf;
}

/* Wait queue for /sys/kernel/security/tomoyo/audit. */
static DECLARE_WAIT_QUEUE_HEAD(tomoyo_log_wait);

/* Structure for audit log. */
struct tomoyo_log {
	struct list_head list;
	char *log;
	int size;
};

/* The list for "struct tomoyo_log". */
static LIST_HEAD(tomoyo_log);

/* Lock for "struct list_head tomoyo_log". */
static DEFINE_SPINLOCK(tomoyo_log_lock);

/* Length of "stuct list_head tomoyo_log". */
static unsigned int tomoyo_log_count;

/**
 * tomoyo_get_audit - Get audit mode.
 *
 * @ns:          Pointer to "struct tomoyo_policy_namespace".
 * @profile:     Profile number.
 * @index:       Index number of functionality.
 * @is_granted:  True if granted log, false otherwise.
 *
 * Returns true if this request should be audited, false otherwise.
 */
static bool tomoyo_get_audit(const struct tomoyo_policy_namespace *ns,
			     const u8 profile, const u8 index,
			     const bool is_granted)
{
	u8 mode;
	const u8 category = tomoyo_index2category[index] +
		TOMOYO_MAX_MAC_INDEX;
	struct tomoyo_profile *p;
	if (!tomoyo_policy_loaded)
		return false;
	p = tomoyo_profile(ns, profile);
	if (tomoyo_log_count >= p->pref[TOMOYO_PREF_MAX_AUDIT_LOG])
		return false;
	mode = p->config[index];
	if (mode == TOMOYO_CONFIG_USE_DEFAULT)
		mode = p->config[category];
	if (mode == TOMOYO_CONFIG_USE_DEFAULT)
		mode = p->default_config;
	if (is_granted)
		return mode & TOMOYO_CONFIG_WANT_GRANT_LOG;
	return mode & TOMOYO_CONFIG_WANT_REJECT_LOG;
}

/**
 * tomoyo_write_log2 - Write an audit log.
 *
 * @r:    Pointer to "struct tomoyo_request_info".
 * @len:  Buffer size needed for @fmt and @args.
 * @fmt:  The printf()'s format string.
 * @args: va_list structure for @fmt.
 *
 * Returns nothing.
 */
void tomoyo_write_log2(struct tomoyo_request_info *r, int len, const char *fmt,
		       va_list args)
{
	char *buf;
	struct tomoyo_log *entry;
	bool quota_exceeded = false;
	if (!tomoyo_get_audit(r->domain->ns, r->profile, r->type, r->granted))
		goto out;
	buf = tomoyo_init_log(r, len, fmt, args);
	if (!buf)
		goto out;
	entry = kzalloc(sizeof(*entry), GFP_NOFS);
	if (!entry) {
		kfree(buf);
		goto out;
	}
	entry->log = buf;
	len = tomoyo_round2(strlen(buf) + 1);
	/*
	 * The entry->size is used for memory quota checks.
	 * Don't go beyond strlen(entry->log).
	 */
	entry->size = len + tomoyo_round2(sizeof(*entry));
	spin_lock(&tomoyo_log_lock);
	if (tomoyo_memory_quota[TOMOYO_MEMORY_AUDIT] &&
	    tomoyo_memory_used[TOMOYO_MEMORY_AUDIT] + entry->size >=
	    tomoyo_memory_quota[TOMOYO_MEMORY_AUDIT]) {
		quota_exceeded = true;
	} else {
		tomoyo_memory_used[TOMOYO_MEMORY_AUDIT] += entry->size;
		list_add_tail(&entry->list, &tomoyo_log);
		tomoyo_log_count++;
	}
	spin_unlock(&tomoyo_log_lock);
	if (quota_exceeded) {
		kfree(buf);
		kfree(entry);
		goto out;
	}
	wake_up(&tomoyo_log_wait);
out:
	return;
}

/**
 * tomoyo_write_log - Write an audit log.
 *
 * @r:   Pointer to "struct tomoyo_request_info".
 * @fmt: The printf()'s format string, followed by parameters.
 *
 * Returns nothing.
 */
void tomoyo_write_log(struct tomoyo_request_info *r, const char *fmt, ...)
{
	va_list args;
	int len;
	va_start(args, fmt);
	len = vsnprintf((char *) &len, 1, fmt, args) + 1;
	va_end(args);
	va_start(args, fmt);
	tomoyo_write_log2(r, len, fmt, args);
	va_end(args);
}

/**
 * tomoyo_read_log - Read an audit log.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
void tomoyo_read_log(struct tomoyo_io_buffer *head)
{
	struct tomoyo_log *ptr = NULL;
	if (head->r.w_pos)
		return;
	kfree(head->read_buf);
	head->read_buf = NULL;
	spin_lock(&tomoyo_log_lock);
	if (!list_empty(&tomoyo_log)) {
		ptr = list_entry(tomoyo_log.next, typeof(*ptr), list);
		list_del(&ptr->list);
		tomoyo_log_count--;
		tomoyo_memory_used[TOMOYO_MEMORY_AUDIT] -= ptr->size;
	}
	spin_unlock(&tomoyo_log_lock);
	if (ptr) {
		head->read_buf = ptr->log;
		head->r.w[head->r.w_pos++] = head->read_buf;
		kfree(ptr);
	}
}

/**
 * tomoyo_poll_log - Wait for an audit log.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table".
 *
 * Returns POLLIN | POLLRDNORM when ready to read an audit log.
 */
int tomoyo_poll_log(struct file *file, poll_table *wait)
{
	if (tomoyo_log_count)
		return POLLIN | POLLRDNORM;
	poll_wait(file, &tomoyo_log_wait, wait);
	if (tomoyo_log_count)
		return POLLIN | POLLRDNORM;
	return 0;
}
