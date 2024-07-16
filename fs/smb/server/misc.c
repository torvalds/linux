// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/kernel.h>
#include <linux/xattr.h>
#include <linux/fs.h>
#include <linux/unicode.h>

#include "misc.h"
#include "smb_common.h"
#include "connection.h"
#include "vfs.h"

#include "mgmt/share_config.h"

/**
 * match_pattern() - compare a string with a pattern which might include
 * wildcard '*' and '?'
 * TODO : implement consideration about DOS_DOT, DOS_QM and DOS_STAR
 *
 * @str:	string to compare with a pattern
 * @len:	string length
 * @pattern:	pattern string which might include wildcard '*' and '?'
 *
 * Return:	0 if pattern matched with the string, otherwise non zero value
 */
int match_pattern(const char *str, size_t len, const char *pattern)
{
	const char *s = str;
	const char *p = pattern;
	bool star = false;

	while (*s && len) {
		switch (*p) {
		case '?':
			s++;
			len--;
			p++;
			break;
		case '*':
			star = true;
			str = s;
			if (!*++p)
				return true;
			pattern = p;
			break;
		default:
			if (tolower(*s) == tolower(*p)) {
				s++;
				len--;
				p++;
			} else {
				if (!star)
					return false;
				str++;
				s = str;
				p = pattern;
			}
			break;
		}
	}

	if (*p == '*')
		++p;
	return !*p;
}

/*
 * is_char_allowed() - check for valid character
 * @ch:		input character to be checked
 *
 * Return:	1 if char is allowed, otherwise 0
 */
static inline int is_char_allowed(char ch)
{
	/* check for control chars, wildcards etc. */
	if (!(ch & 0x80) &&
	    (ch <= 0x1f ||
	     ch == '?' || ch == '"' || ch == '<' ||
	     ch == '>' || ch == '|' || ch == '*'))
		return 0;

	return 1;
}

int ksmbd_validate_filename(char *filename)
{
	while (*filename) {
		char c = *filename;

		filename++;
		if (!is_char_allowed(c)) {
			ksmbd_debug(VFS, "File name validation failed: 0x%x\n", c);
			return -ENOENT;
		}
	}

	return 0;
}

static int ksmbd_validate_stream_name(char *stream_name)
{
	while (*stream_name) {
		char c = *stream_name;

		stream_name++;
		if (c == '/' || c == ':' || c == '\\') {
			pr_err("Stream name validation failed: %c\n", c);
			return -ENOENT;
		}
	}

	return 0;
}

int parse_stream_name(char *filename, char **stream_name, int *s_type)
{
	char *stream_type;
	char *s_name;
	int rc = 0;

	s_name = filename;
	filename = strsep(&s_name, ":");
	ksmbd_debug(SMB, "filename : %s, streams : %s\n", filename, s_name);
	if (strchr(s_name, ':')) {
		stream_type = s_name;
		s_name = strsep(&stream_type, ":");

		rc = ksmbd_validate_stream_name(s_name);
		if (rc < 0) {
			rc = -ENOENT;
			goto out;
		}

		ksmbd_debug(SMB, "stream name : %s, stream type : %s\n", s_name,
			    stream_type);
		if (!strncasecmp("$data", stream_type, 5))
			*s_type = DATA_STREAM;
		else if (!strncasecmp("$index_allocation", stream_type, 17))
			*s_type = DIR_STREAM;
		else
			rc = -ENOENT;
	}

	*stream_name = s_name;
out:
	return rc;
}

/**
 * convert_to_nt_pathname() - extract and return windows path string
 *      whose share directory prefix was removed from file path
 * @share: ksmbd_share_config pointer
 * @path: path to report
 *
 * Return : windows path string or error
 */

char *convert_to_nt_pathname(struct ksmbd_share_config *share,
			     const struct path *path)
{
	char *pathname, *ab_pathname, *nt_pathname;
	int share_path_len = share->path_sz;

	pathname = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!pathname)
		return ERR_PTR(-EACCES);

	ab_pathname = d_path(path, pathname, PATH_MAX);
	if (IS_ERR(ab_pathname)) {
		nt_pathname = ERR_PTR(-EACCES);
		goto free_pathname;
	}

	if (strncmp(ab_pathname, share->path, share_path_len)) {
		nt_pathname = ERR_PTR(-EACCES);
		goto free_pathname;
	}

	nt_pathname = kzalloc(strlen(&ab_pathname[share_path_len]) + 2, GFP_KERNEL);
	if (!nt_pathname) {
		nt_pathname = ERR_PTR(-ENOMEM);
		goto free_pathname;
	}
	if (ab_pathname[share_path_len] == '\0')
		strcpy(nt_pathname, "/");
	strcat(nt_pathname, &ab_pathname[share_path_len]);

	ksmbd_conv_path_to_windows(nt_pathname);

free_pathname:
	kfree(pathname);
	return nt_pathname;
}

int get_nlink(struct kstat *st)
{
	int nlink;

	nlink = st->nlink;
	if (S_ISDIR(st->mode))
		nlink--;

	return nlink;
}

void ksmbd_conv_path_to_unix(char *path)
{
	strreplace(path, '\\', '/');
}

void ksmbd_strip_last_slash(char *path)
{
	int len = strlen(path);

	while (len && path[len - 1] == '/') {
		path[len - 1] = '\0';
		len--;
	}
}

void ksmbd_conv_path_to_windows(char *path)
{
	strreplace(path, '/', '\\');
}

char *ksmbd_casefold_sharename(struct unicode_map *um, const char *name)
{
	char *cf_name;
	int cf_len;

	cf_name = kzalloc(KSMBD_REQ_MAX_SHARE_NAME, GFP_KERNEL);
	if (!cf_name)
		return ERR_PTR(-ENOMEM);

	if (IS_ENABLED(CONFIG_UNICODE) && um) {
		const struct qstr q_name = {.name = name, .len = strlen(name)};

		cf_len = utf8_casefold(um, &q_name, cf_name,
				       KSMBD_REQ_MAX_SHARE_NAME);
		if (cf_len < 0)
			goto out_ascii;

		return cf_name;
	}

out_ascii:
	cf_len = strscpy(cf_name, name, KSMBD_REQ_MAX_SHARE_NAME);
	if (cf_len < 0) {
		kfree(cf_name);
		return ERR_PTR(-E2BIG);
	}

	for (; *cf_name; ++cf_name)
		*cf_name = isascii(*cf_name) ? tolower(*cf_name) : *cf_name;
	return cf_name - cf_len;
}

/**
 * ksmbd_extract_sharename() - get share name from tree connect request
 * @treename:	buffer containing tree name and share name
 *
 * Return:      share name on success, otherwise error
 */
char *ksmbd_extract_sharename(struct unicode_map *um, const char *treename)
{
	const char *name = treename, *pos = strrchr(name, '\\');

	if (pos)
		name = (pos + 1);

	/* caller has to free the memory */
	return ksmbd_casefold_sharename(um, name);
}

/**
 * convert_to_unix_name() - convert windows name to unix format
 * @share:	ksmbd_share_config pointer
 * @name:	file name that is relative to share
 *
 * Return:	converted name on success, otherwise NULL
 */
char *convert_to_unix_name(struct ksmbd_share_config *share, const char *name)
{
	int no_slash = 0, name_len, path_len;
	char *new_name;

	if (name[0] == '/')
		name++;

	path_len = share->path_sz;
	name_len = strlen(name);
	new_name = kmalloc(path_len + name_len + 2, GFP_KERNEL);
	if (!new_name)
		return new_name;

	memcpy(new_name, share->path, path_len);
	if (new_name[path_len - 1] != '/') {
		new_name[path_len] = '/';
		no_slash = 1;
	}

	memcpy(new_name + path_len + no_slash, name, name_len);
	path_len += name_len + no_slash;
	new_name[path_len] = 0x00;
	return new_name;
}

char *ksmbd_convert_dir_info_name(struct ksmbd_dir_info *d_info,
				  const struct nls_table *local_nls,
				  int *conv_len)
{
	char *conv;
	int  sz = min(4 * d_info->name_len, PATH_MAX);

	if (!sz)
		return NULL;

	conv = kmalloc(sz, GFP_KERNEL);
	if (!conv)
		return NULL;

	/* XXX */
	*conv_len = smbConvertToUTF16((__le16 *)conv, d_info->name,
				      d_info->name_len, local_nls, 0);
	*conv_len *= 2;

	/* We allocate buffer twice bigger than needed. */
	conv[*conv_len] = 0x00;
	conv[*conv_len + 1] = 0x00;
	return conv;
}

/*
 * Convert the NT UTC (based 1601-01-01, in hundred nanosecond units)
 * into Unix UTC (based 1970-01-01, in seconds).
 */
struct timespec64 ksmbd_NTtimeToUnix(__le64 ntutc)
{
	struct timespec64 ts;

	/* Subtract the NTFS time offset, then convert to 1s intervals. */
	s64 t = le64_to_cpu(ntutc) - NTFS_TIME_OFFSET;
	u64 abs_t;

	/*
	 * Unfortunately can not use normal 64 bit division on 32 bit arch, but
	 * the alternative, do_div, does not work with negative numbers so have
	 * to special case them
	 */
	if (t < 0) {
		abs_t = -t;
		ts.tv_nsec = do_div(abs_t, 10000000) * 100;
		ts.tv_nsec = -ts.tv_nsec;
		ts.tv_sec = -abs_t;
	} else {
		abs_t = t;
		ts.tv_nsec = do_div(abs_t, 10000000) * 100;
		ts.tv_sec = abs_t;
	}

	return ts;
}

/* Convert the Unix UTC into NT UTC. */
inline u64 ksmbd_UnixTimeToNT(struct timespec64 t)
{
	/* Convert to 100ns intervals and then add the NTFS time offset. */
	return (u64)t.tv_sec * 10000000 + t.tv_nsec / 100 + NTFS_TIME_OFFSET;
}

inline long long ksmbd_systime(void)
{
	struct timespec64	ts;

	ktime_get_real_ts64(&ts);
	return ksmbd_UnixTimeToNT(ts);
}
