/*
 * security/tomoyo/audit.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include "common.h"
#include <linux/slab.h>

/**
 * tomoyo_print_bprm - Print "struct linux_binprm" for auditing.
 *
 * @bprm: Pointer to "struct linux_binprm".
 * @dump: Pointer to "struct tomoyo_page_dump".
 *
 * Returns the contents of @bprm on success, NULL otherwise.
 *
 * This function uses kzalloc(), so caller must kfree() if this function
 * didn't return NULL.
 */
static char *tomoyo_print_bprm(struct linux_binprm *bprm,
			       struct tomoyo_page_dump *dump)
{
	static const int tomoyo_buffer_len = 4096 * 2;
	char *buffer = kzalloc(tomoyo_buffer_len, GFP_NOFS);
	char *cp;
	char *last_start;
	int len;
	unsigned long pos = bprm->p;
	int offset = pos % PAGE_SIZE;
	int argv_count = bprm->argc;
	int envp_count = bprm->envc;
	bool truncated = false;
	if (!buffer)
		return NULL;
	len = snprintf(buffer, tomoyo_buffer_len - 1, "argv[]={ ");
	cp = buffer + len;
	if (!argv_count) {
		memmove(cp, "} envp[]={ ", 11);
		cp += 11;
	}
	last_start = cp;
	while (argv_count || envp_count) {
		if (!tomoyo_dump_page(bprm, pos, dump))
			goto out;
		pos += PAGE_SIZE - offset;
		/* Read. */
		while (offset < PAGE_SIZE) {
			const char *kaddr = dump->data;
			const unsigned char c = kaddr[offset++];
			if (cp == last_start)
				*cp++ = '"';
			if (cp >= buffer + tomoyo_buffer_len - 32) {
				/* Reserve some room for "..." string. */
				truncated = true;
			} else if (c == '\\') {
				*cp++ = '\\';
				*cp++ = '\\';
			} else if (c > ' ' && c < 127) {
				*cp++ = c;
			} else if (!c) {
				*cp++ = '"';
				*cp++ = ' ';
				last_start = cp;
			} else {
				*cp++ = '\\';
				*cp++ = (c >> 6) + '0';
				*cp++ = ((c >> 3) & 7) + '0';
				*cp++ = (c & 7) + '0';
			}
			if (c)
				continue;
			if (argv_count) {
				if (--argv_count == 0) {
					if (truncated) {
						cp = last_start;
						memmove(cp, "... ", 4);
						cp += 4;
					}
					memmove(cp, "} envp[]={ ", 11);
					cp += 11;
					last_start = cp;
					truncated = false;
				}
			} else if (envp_count) {
				if (--envp_count == 0) {
					if (truncated) {
						cp = last_start;
						memmove(cp, "... ", 4);
						cp += 4;
					}
				}
			}
			if (!argv_count && !envp_count)
				break;
		}
		offset = 0;
	}
	*cp++ = '}';
	*cp = '\0';
	return buffer;
out:
	snprintf(buffer, tomoyo_buffer_len - 1,
		 "argv[]={ ... } envp[]= { ... }");
	return buffer;
}

/**
 * tomoyo_filetype - Get string representation of file type.
 *
 * @mode: Mode value for stat().
 *
 * Returns file type string.
 */
static inline const char *tomoyo_filetype(const mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
	case 0:
		return tomoyo_condition_keyword[TOMOYO_TYPE_IS_FILE];
	case S_IFDIR:
		return tomoyo_condition_keyword[TOMOYO_TYPE_IS_DIRECTORY];
	case S_IFLNK:
		return tomoyo_condition_keyword[TOMOYO_TYPE_IS_SYMLINK];
	case S_IFIFO:
		return tomoyo_condition_keyword[TOMOYO_TYPE_IS_FIFO];
	case S_IFSOCK:
		return tomoyo_condition_keyword[TOMOYO_TYPE_IS_SOCKET];
	case S_IFBLK:
		return tomoyo_condition_keyword[TOMOYO_TYPE_IS_BLOCK_DEV];
	case S_IFCHR:
		return tomoyo_condition_keyword[TOMOYO_TYPE_IS_CHAR_DEV];
	}
	return "unknown"; /* This should not happen. */
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
	struct tomoyo_obj_info *obj = r->obj;
	static const int tomoyo_buffer_len = 4096;
	char *buffer = kmalloc(tomoyo_buffer_len, GFP_NOFS);
	int pos;
	u8 i;
	if (!buffer)
		return NULL;
	{
		struct timeval tv;
		do_gettimeofday(&tv);
		tomoyo_convert_time(tv.tv_sec, &stamp);
	}
	pos = snprintf(buffer, tomoyo_buffer_len - 1,
		       "#%04u/%02u/%02u %02u:%02u:%02u# profile=%u mode=%s "
		       "granted=%s (global-pid=%u) task={ pid=%u ppid=%u "
		       "uid=%u gid=%u euid=%u egid=%u suid=%u sgid=%u "
		       "fsuid=%u fsgid=%u }", stamp.year, stamp.month,
		       stamp.day, stamp.hour, stamp.min, stamp.sec, r->profile,
		       tomoyo_mode[r->mode], tomoyo_yesno(r->granted), gpid,
		       tomoyo_sys_getpid(), tomoyo_sys_getppid(),
		       current_uid(), current_gid(), current_euid(),
		       current_egid(), current_suid(), current_sgid(),
		       current_fsuid(), current_fsgid());
	if (!obj)
		goto no_obj_info;
	if (!obj->validate_done) {
		tomoyo_get_attributes(obj);
		obj->validate_done = true;
	}
	for (i = 0; i < TOMOYO_MAX_PATH_STAT; i++) {
		struct tomoyo_mini_stat *stat;
		unsigned int dev;
		mode_t mode;
		if (!obj->stat_valid[i])
			continue;
		stat = &obj->stat[i];
		dev = stat->dev;
		mode = stat->mode;
		if (i & 1) {
			pos += snprintf(buffer + pos,
					tomoyo_buffer_len - 1 - pos,
					" path%u.parent={ uid=%u gid=%u "
					"ino=%lu perm=0%o }", (i >> 1) + 1,
					stat->uid, stat->gid, (unsigned long)
					stat->ino, stat->mode & S_IALLUGO);
			continue;
		}
		pos += snprintf(buffer + pos, tomoyo_buffer_len - 1 - pos,
				" path%u={ uid=%u gid=%u ino=%lu major=%u"
				" minor=%u perm=0%o type=%s", (i >> 1) + 1,
				stat->uid, stat->gid, (unsigned long)
				stat->ino, MAJOR(dev), MINOR(dev),
				mode & S_IALLUGO, tomoyo_filetype(mode));
		if (S_ISCHR(mode) || S_ISBLK(mode)) {
			dev = stat->rdev;
			pos += snprintf(buffer + pos,
					tomoyo_buffer_len - 1 - pos,
					" dev_major=%u dev_minor=%u",
					MAJOR(dev), MINOR(dev));
		}
		pos += snprintf(buffer + pos, tomoyo_buffer_len - 1 - pos,
				" }");
	}
no_obj_info:
	if (pos < tomoyo_buffer_len - 1)
		return buffer;
	kfree(buffer);
	return NULL;
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
	char *bprm_info = NULL;
	const char *header = NULL;
	char *realpath = NULL;
	const char *symlink = NULL;
	int pos;
	const char *domainname = r->domain->domainname->name;
	header = tomoyo_print_header(r);
	if (!header)
		return NULL;
	/* +10 is for '\n' etc. and '\0'. */
	len += strlen(domainname) + strlen(header) + 10;
	if (r->ee) {
		struct file *file = r->ee->bprm->file;
		realpath = tomoyo_realpath_from_path(&file->f_path);
		bprm_info = tomoyo_print_bprm(r->ee->bprm, &r->ee->dump);
		if (!realpath || !bprm_info)
			goto out;
		/* +80 is for " exec={ realpath=\"%s\" argc=%d envc=%d %s }" */
		len += strlen(realpath) + 80 + strlen(bprm_info);
	} else if (r->obj && r->obj->symlink_target) {
		symlink = r->obj->symlink_target->name;
		/* +18 is for " symlink.target=\"%s\"" */
		len += 18 + strlen(symlink);
	}
	len = tomoyo_round2(len);
	buf = kzalloc(len, GFP_NOFS);
	if (!buf)
		goto out;
	len--;
	pos = snprintf(buf, len, "%s", header);
	if (realpath) {
		struct linux_binprm *bprm = r->ee->bprm;
		pos += snprintf(buf + pos, len - pos,
				" exec={ realpath=\"%s\" argc=%d envc=%d %s }",
				realpath, bprm->argc, bprm->envc, bprm_info);
	} else if (symlink)
		pos += snprintf(buf + pos, len - pos, " symlink.target=\"%s\"",
				symlink);
	pos += snprintf(buf + pos, len - pos, "\n%s\n", domainname);
	vsnprintf(buf + pos, len - pos, fmt, args);
out:
	kfree(realpath);
	kfree(bprm_info);
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
			     const struct tomoyo_acl_info *matched_acl,
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
	if (is_granted && matched_acl && matched_acl->cond &&
	    matched_acl->cond->grant_log != TOMOYO_GRANTLOG_AUTO)
		return matched_acl->cond->grant_log == TOMOYO_GRANTLOG_YES;
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
	if (!tomoyo_get_audit(r->domain->ns, r->profile, r->type,
			      r->matched_acl, r->granted))
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
