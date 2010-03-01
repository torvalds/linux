/*
 * security/tomoyo/common.c
 *
 * Common functions for TOMOYO.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 2.2.0   2009/04/01
 *
 */

#include <linux/uaccess.h>
#include <linux/security.h>
#include <linux/hardirq.h>
#include "realpath.h"
#include "common.h"
#include "tomoyo.h"

/* Has loading policy done? */
bool tomoyo_policy_loaded;

/* String table for functionality that takes 4 modes. */
static const char *tomoyo_mode_4[4] = {
	"disabled", "learning", "permissive", "enforcing"
};
/* String table for functionality that takes 2 modes. */
static const char *tomoyo_mode_2[4] = {
	"disabled", "enabled", "enabled", "enabled"
};

/*
 * tomoyo_control_array is a static data which contains
 *
 *  (1) functionality name used by /sys/kernel/security/tomoyo/profile .
 *  (2) initial values for "struct tomoyo_profile".
 *  (3) max values for "struct tomoyo_profile".
 */
static struct {
	const char *keyword;
	unsigned int current_value;
	const unsigned int max_value;
} tomoyo_control_array[TOMOYO_MAX_CONTROL_INDEX] = {
	[TOMOYO_MAC_FOR_FILE]     = { "MAC_FOR_FILE",        0,       3 },
	[TOMOYO_MAX_ACCEPT_ENTRY] = { "MAX_ACCEPT_ENTRY", 2048, INT_MAX },
	[TOMOYO_VERBOSE]          = { "TOMOYO_VERBOSE",      1,       1 },
};

/*
 * tomoyo_profile is a structure which is used for holding the mode of access
 * controls. TOMOYO has 4 modes: disabled, learning, permissive, enforcing.
 * An administrator can define up to 256 profiles.
 * The ->profile of "struct tomoyo_domain_info" is used for remembering
 * the profile's number (0 - 255) assigned to that domain.
 */
static struct tomoyo_profile {
	unsigned int value[TOMOYO_MAX_CONTROL_INDEX];
	const struct tomoyo_path_info *comment;
} *tomoyo_profile_ptr[TOMOYO_MAX_PROFILES];

/* Permit policy management by non-root user? */
static bool tomoyo_manage_by_non_root;

/* Utility functions. */

/* Open operation for /sys/kernel/security/tomoyo/ interface. */
static int tomoyo_open_control(const u8 type, struct file *file);
/* Close /sys/kernel/security/tomoyo/ interface. */
static int tomoyo_close_control(struct file *file);
/* Read operation for /sys/kernel/security/tomoyo/ interface. */
static int tomoyo_read_control(struct file *file, char __user *buffer,
			       const int buffer_len);
/* Write operation for /sys/kernel/security/tomoyo/ interface. */
static int tomoyo_write_control(struct file *file, const char __user *buffer,
				const int buffer_len);

/**
 * tomoyo_is_byte_range - Check whether the string isa \ooo style octal value.
 *
 * @str: Pointer to the string.
 *
 * Returns true if @str is a \ooo style octal value, false otherwise.
 *
 * TOMOYO uses \ooo style representation for 0x01 - 0x20 and 0x7F - 0xFF.
 * This function verifies that \ooo is in valid range.
 */
static inline bool tomoyo_is_byte_range(const char *str)
{
	return *str >= '0' && *str++ <= '3' &&
		*str >= '0' && *str++ <= '7' &&
		*str >= '0' && *str <= '7';
}

/**
 * tomoyo_is_alphabet_char - Check whether the character is an alphabet.
 *
 * @c: The character to check.
 *
 * Returns true if @c is an alphabet character, false otherwise.
 */
static inline bool tomoyo_is_alphabet_char(const char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * tomoyo_make_byte - Make byte value from three octal characters.
 *
 * @c1: The first character.
 * @c2: The second character.
 * @c3: The third character.
 *
 * Returns byte value.
 */
static inline u8 tomoyo_make_byte(const u8 c1, const u8 c2, const u8 c3)
{
	return ((c1 - '0') << 6) + ((c2 - '0') << 3) + (c3 - '0');
}

/**
 * tomoyo_str_starts - Check whether the given string starts with the given keyword.
 *
 * @src:  Pointer to pointer to the string.
 * @find: Pointer to the keyword.
 *
 * Returns true if @src starts with @find, false otherwise.
 *
 * The @src is updated to point the first character after the @find
 * if @src starts with @find.
 */
static bool tomoyo_str_starts(char **src, const char *find)
{
	const int len = strlen(find);
	char *tmp = *src;

	if (strncmp(tmp, find, len))
		return false;
	tmp += len;
	*src = tmp;
	return true;
}

/**
 * tomoyo_normalize_line - Format string.
 *
 * @buffer: The line to normalize.
 *
 * Leading and trailing whitespaces are removed.
 * Multiple whitespaces are packed into single space.
 *
 * Returns nothing.
 */
static void tomoyo_normalize_line(unsigned char *buffer)
{
	unsigned char *sp = buffer;
	unsigned char *dp = buffer;
	bool first = true;

	while (tomoyo_is_invalid(*sp))
		sp++;
	while (*sp) {
		if (!first)
			*dp++ = ' ';
		first = false;
		while (tomoyo_is_valid(*sp))
			*dp++ = *sp++;
		while (tomoyo_is_invalid(*sp))
			sp++;
	}
	*dp = '\0';
}

/**
 * tomoyo_is_correct_path - Validate a pathname.
 * @filename:     The pathname to check.
 * @start_type:   Should the pathname start with '/'?
 *                1 = must / -1 = must not / 0 = don't care
 * @pattern_type: Can the pathname contain a wildcard?
 *                1 = must / -1 = must not / 0 = don't care
 * @end_type:     Should the pathname end with '/'?
 *                1 = must / -1 = must not / 0 = don't care
 * @function:     The name of function calling me.
 *
 * Check whether the given filename follows the naming rules.
 * Returns true if @filename follows the naming rules, false otherwise.
 */
bool tomoyo_is_correct_path(const char *filename, const s8 start_type,
			    const s8 pattern_type, const s8 end_type,
			    const char *function)
{
	const char *const start = filename;
	bool in_repetition = false;
	bool contains_pattern = false;
	unsigned char c;
	unsigned char d;
	unsigned char e;
	const char *original_filename = filename;

	if (!filename)
		goto out;
	c = *filename;
	if (start_type == 1) { /* Must start with '/' */
		if (c != '/')
			goto out;
	} else if (start_type == -1) { /* Must not start with '/' */
		if (c == '/')
			goto out;
	}
	if (c)
		c = *(filename + strlen(filename) - 1);
	if (end_type == 1) { /* Must end with '/' */
		if (c != '/')
			goto out;
	} else if (end_type == -1) { /* Must not end with '/' */
		if (c == '/')
			goto out;
	}
	while (1) {
		c = *filename++;
		if (!c)
			break;
		if (c == '\\') {
			c = *filename++;
			switch (c) {
			case '\\':  /* "\\" */
				continue;
			case '$':   /* "\$" */
			case '+':   /* "\+" */
			case '?':   /* "\?" */
			case '*':   /* "\*" */
			case '@':   /* "\@" */
			case 'x':   /* "\x" */
			case 'X':   /* "\X" */
			case 'a':   /* "\a" */
			case 'A':   /* "\A" */
			case '-':   /* "\-" */
				if (pattern_type == -1)
					break; /* Must not contain pattern */
				contains_pattern = true;
				continue;
			case '{':   /* "/\{" */
				if (filename - 3 < start ||
				    *(filename - 3) != '/')
					break;
				if (pattern_type == -1)
					break; /* Must not contain pattern */
				contains_pattern = true;
				in_repetition = true;
				continue;
			case '}':   /* "\}/" */
				if (*filename != '/')
					break;
				if (!in_repetition)
					break;
				in_repetition = false;
				continue;
			case '0':   /* "\ooo" */
			case '1':
			case '2':
			case '3':
				d = *filename++;
				if (d < '0' || d > '7')
					break;
				e = *filename++;
				if (e < '0' || e > '7')
					break;
				c = tomoyo_make_byte(c, d, e);
				if (tomoyo_is_invalid(c))
					continue; /* pattern is not \000 */
			}
			goto out;
		} else if (in_repetition && c == '/') {
			goto out;
		} else if (tomoyo_is_invalid(c)) {
			goto out;
		}
	}
	if (pattern_type == 1) { /* Must contain pattern */
		if (!contains_pattern)
			goto out;
	}
	if (in_repetition)
		goto out;
	return true;
 out:
	printk(KERN_DEBUG "%s: Invalid pathname '%s'\n", function,
	       original_filename);
	return false;
}

/**
 * tomoyo_is_correct_domain - Check whether the given domainname follows the naming rules.
 * @domainname:   The domainname to check.
 * @function:     The name of function calling me.
 *
 * Returns true if @domainname follows the naming rules, false otherwise.
 */
bool tomoyo_is_correct_domain(const unsigned char *domainname,
			      const char *function)
{
	unsigned char c;
	unsigned char d;
	unsigned char e;
	const char *org_domainname = domainname;

	if (!domainname || strncmp(domainname, TOMOYO_ROOT_NAME,
				   TOMOYO_ROOT_NAME_LEN))
		goto out;
	domainname += TOMOYO_ROOT_NAME_LEN;
	if (!*domainname)
		return true;
	do {
		if (*domainname++ != ' ')
			goto out;
		if (*domainname++ != '/')
			goto out;
		while ((c = *domainname) != '\0' && c != ' ') {
			domainname++;
			if (c == '\\') {
				c = *domainname++;
				switch ((c)) {
				case '\\':  /* "\\" */
					continue;
				case '0':   /* "\ooo" */
				case '1':
				case '2':
				case '3':
					d = *domainname++;
					if (d < '0' || d > '7')
						break;
					e = *domainname++;
					if (e < '0' || e > '7')
						break;
					c = tomoyo_make_byte(c, d, e);
					if (tomoyo_is_invalid(c))
						/* pattern is not \000 */
						continue;
				}
				goto out;
			} else if (tomoyo_is_invalid(c)) {
				goto out;
			}
		}
	} while (*domainname);
	return true;
 out:
	printk(KERN_DEBUG "%s: Invalid domainname '%s'\n", function,
	       org_domainname);
	return false;
}

/**
 * tomoyo_is_domain_def - Check whether the given token can be a domainname.
 *
 * @buffer: The token to check.
 *
 * Returns true if @buffer possibly be a domainname, false otherwise.
 */
bool tomoyo_is_domain_def(const unsigned char *buffer)
{
	return !strncmp(buffer, TOMOYO_ROOT_NAME, TOMOYO_ROOT_NAME_LEN);
}

/**
 * tomoyo_find_domain - Find a domain by the given name.
 *
 * @domainname: The domainname to find.
 *
 * Caller must call down_read(&tomoyo_domain_list_lock); or
 * down_write(&tomoyo_domain_list_lock); .
 *
 * Returns pointer to "struct tomoyo_domain_info" if found, NULL otherwise.
 */
struct tomoyo_domain_info *tomoyo_find_domain(const char *domainname)
{
	struct tomoyo_domain_info *domain;
	struct tomoyo_path_info name;

	name.name = domainname;
	tomoyo_fill_path_info(&name);
	list_for_each_entry(domain, &tomoyo_domain_list, list) {
		if (!domain->is_deleted &&
		    !tomoyo_pathcmp(&name, domain->domainname))
			return domain;
	}
	return NULL;
}

/**
 * tomoyo_const_part_length - Evaluate the initial length without a pattern in a token.
 *
 * @filename: The string to evaluate.
 *
 * Returns the initial length without a pattern in @filename.
 */
static int tomoyo_const_part_length(const char *filename)
{
	char c;
	int len = 0;

	if (!filename)
		return 0;
	while ((c = *filename++) != '\0') {
		if (c != '\\') {
			len++;
			continue;
		}
		c = *filename++;
		switch (c) {
		case '\\':  /* "\\" */
			len += 2;
			continue;
		case '0':   /* "\ooo" */
		case '1':
		case '2':
		case '3':
			c = *filename++;
			if (c < '0' || c > '7')
				break;
			c = *filename++;
			if (c < '0' || c > '7')
				break;
			len += 4;
			continue;
		}
		break;
	}
	return len;
}

/**
 * tomoyo_fill_path_info - Fill in "struct tomoyo_path_info" members.
 *
 * @ptr: Pointer to "struct tomoyo_path_info" to fill in.
 *
 * The caller sets "struct tomoyo_path_info"->name.
 */
void tomoyo_fill_path_info(struct tomoyo_path_info *ptr)
{
	const char *name = ptr->name;
	const int len = strlen(name);

	ptr->const_len = tomoyo_const_part_length(name);
	ptr->is_dir = len && (name[len - 1] == '/');
	ptr->is_patterned = (ptr->const_len < len);
	ptr->hash = full_name_hash(name, len);
}

/**
 * tomoyo_file_matches_pattern2 - Pattern matching without '/' character
 * and "\-" pattern.
 *
 * @filename:     The start of string to check.
 * @filename_end: The end of string to check.
 * @pattern:      The start of pattern to compare.
 * @pattern_end:  The end of pattern to compare.
 *
 * Returns true if @filename matches @pattern, false otherwise.
 */
static bool tomoyo_file_matches_pattern2(const char *filename,
					 const char *filename_end,
					 const char *pattern,
					 const char *pattern_end)
{
	while (filename < filename_end && pattern < pattern_end) {
		char c;
		if (*pattern != '\\') {
			if (*filename++ != *pattern++)
				return false;
			continue;
		}
		c = *filename;
		pattern++;
		switch (*pattern) {
			int i;
			int j;
		case '?':
			if (c == '/') {
				return false;
			} else if (c == '\\') {
				if (filename[1] == '\\')
					filename++;
				else if (tomoyo_is_byte_range(filename + 1))
					filename += 3;
				else
					return false;
			}
			break;
		case '\\':
			if (c != '\\')
				return false;
			if (*++filename != '\\')
				return false;
			break;
		case '+':
			if (!isdigit(c))
				return false;
			break;
		case 'x':
			if (!isxdigit(c))
				return false;
			break;
		case 'a':
			if (!tomoyo_is_alphabet_char(c))
				return false;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
			if (c == '\\' && tomoyo_is_byte_range(filename + 1)
			    && strncmp(filename + 1, pattern, 3) == 0) {
				filename += 3;
				pattern += 2;
				break;
			}
			return false; /* Not matched. */
		case '*':
		case '@':
			for (i = 0; i <= filename_end - filename; i++) {
				if (tomoyo_file_matches_pattern2(
						    filename + i, filename_end,
						    pattern + 1, pattern_end))
					return true;
				c = filename[i];
				if (c == '.' && *pattern == '@')
					break;
				if (c != '\\')
					continue;
				if (filename[i + 1] == '\\')
					i++;
				else if (tomoyo_is_byte_range(filename + i + 1))
					i += 3;
				else
					break; /* Bad pattern. */
			}
			return false; /* Not matched. */
		default:
			j = 0;
			c = *pattern;
			if (c == '$') {
				while (isdigit(filename[j]))
					j++;
			} else if (c == 'X') {
				while (isxdigit(filename[j]))
					j++;
			} else if (c == 'A') {
				while (tomoyo_is_alphabet_char(filename[j]))
					j++;
			}
			for (i = 1; i <= j; i++) {
				if (tomoyo_file_matches_pattern2(
						    filename + i, filename_end,
						    pattern + 1, pattern_end))
					return true;
			}
			return false; /* Not matched or bad pattern. */
		}
		filename++;
		pattern++;
	}
	while (*pattern == '\\' &&
	       (*(pattern + 1) == '*' || *(pattern + 1) == '@'))
		pattern += 2;
	return filename == filename_end && pattern == pattern_end;
}

/**
 * tomoyo_file_matches_pattern - Pattern matching without without '/' character.
 *
 * @filename:     The start of string to check.
 * @filename_end: The end of string to check.
 * @pattern:      The start of pattern to compare.
 * @pattern_end:  The end of pattern to compare.
 *
 * Returns true if @filename matches @pattern, false otherwise.
 */
static bool tomoyo_file_matches_pattern(const char *filename,
					   const char *filename_end,
					   const char *pattern,
					   const char *pattern_end)
{
	const char *pattern_start = pattern;
	bool first = true;
	bool result;

	while (pattern < pattern_end - 1) {
		/* Split at "\-" pattern. */
		if (*pattern++ != '\\' || *pattern++ != '-')
			continue;
		result = tomoyo_file_matches_pattern2(filename,
						      filename_end,
						      pattern_start,
						      pattern - 2);
		if (first)
			result = !result;
		if (result)
			return false;
		first = false;
		pattern_start = pattern;
	}
	result = tomoyo_file_matches_pattern2(filename, filename_end,
					      pattern_start, pattern_end);
	return first ? result : !result;
}

/**
 * tomoyo_path_matches_pattern2 - Do pathname pattern matching.
 *
 * @f: The start of string to check.
 * @p: The start of pattern to compare.
 *
 * Returns true if @f matches @p, false otherwise.
 */
static bool tomoyo_path_matches_pattern2(const char *f, const char *p)
{
	const char *f_delimiter;
	const char *p_delimiter;

	while (*f && *p) {
		f_delimiter = strchr(f, '/');
		if (!f_delimiter)
			f_delimiter = f + strlen(f);
		p_delimiter = strchr(p, '/');
		if (!p_delimiter)
			p_delimiter = p + strlen(p);
		if (*p == '\\' && *(p + 1) == '{')
			goto recursive;
		if (!tomoyo_file_matches_pattern(f, f_delimiter, p,
						 p_delimiter))
			return false;
		f = f_delimiter;
		if (*f)
			f++;
		p = p_delimiter;
		if (*p)
			p++;
	}
	/* Ignore trailing "\*" and "\@" in @pattern. */
	while (*p == '\\' &&
	       (*(p + 1) == '*' || *(p + 1) == '@'))
		p += 2;
	return !*f && !*p;
 recursive:
	/*
	 * The "\{" pattern is permitted only after '/' character.
	 * This guarantees that below "*(p - 1)" is safe.
	 * Also, the "\}" pattern is permitted only before '/' character
	 * so that "\{" + "\}" pair will not break the "\-" operator.
	 */
	if (*(p - 1) != '/' || p_delimiter <= p + 3 || *p_delimiter != '/' ||
	    *(p_delimiter - 1) != '}' || *(p_delimiter - 2) != '\\')
		return false; /* Bad pattern. */
	do {
		/* Compare current component with pattern. */
		if (!tomoyo_file_matches_pattern(f, f_delimiter, p + 2,
						 p_delimiter - 2))
			break;
		/* Proceed to next component. */
		f = f_delimiter;
		if (!*f)
			break;
		f++;
		/* Continue comparison. */
		if (tomoyo_path_matches_pattern2(f, p_delimiter + 1))
			return true;
		f_delimiter = strchr(f, '/');
	} while (f_delimiter);
	return false; /* Not matched. */
}

/**
 * tomoyo_path_matches_pattern - Check whether the given filename matches the given pattern.
 *
 * @filename: The filename to check.
 * @pattern:  The pattern to compare.
 *
 * Returns true if matches, false otherwise.
 *
 * The following patterns are available.
 *   \\     \ itself.
 *   \ooo   Octal representation of a byte.
 *   \*     Zero or more repetitions of characters other than '/'.
 *   \@     Zero or more repetitions of characters other than '/' or '.'.
 *   \?     1 byte character other than '/'.
 *   \$     One or more repetitions of decimal digits.
 *   \+     1 decimal digit.
 *   \X     One or more repetitions of hexadecimal digits.
 *   \x     1 hexadecimal digit.
 *   \A     One or more repetitions of alphabet characters.
 *   \a     1 alphabet character.
 *
 *   \-     Subtraction operator.
 *
 *   /\{dir\}/   '/' + 'One or more repetitions of dir/' (e.g. /dir/ /dir/dir/
 *               /dir/dir/dir/ ).
 */
bool tomoyo_path_matches_pattern(const struct tomoyo_path_info *filename,
				 const struct tomoyo_path_info *pattern)
{
	const char *f = filename->name;
	const char *p = pattern->name;
	const int len = pattern->const_len;

	/* If @pattern doesn't contain pattern, I can use strcmp(). */
	if (!pattern->is_patterned)
		return !tomoyo_pathcmp(filename, pattern);
	/* Don't compare directory and non-directory. */
	if (filename->is_dir != pattern->is_dir)
		return false;
	/* Compare the initial length without patterns. */
	if (strncmp(f, p, len))
		return false;
	f += len;
	p += len;
	return tomoyo_path_matches_pattern2(f, p);
}

/**
 * tomoyo_io_printf - Transactional printf() to "struct tomoyo_io_buffer" structure.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @fmt:  The printf()'s format string, followed by parameters.
 *
 * Returns true if output was written, false otherwise.
 *
 * The snprintf() will truncate, but tomoyo_io_printf() won't.
 */
bool tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt, ...)
{
	va_list args;
	int len;
	int pos = head->read_avail;
	int size = head->readbuf_size - pos;

	if (size <= 0)
		return false;
	va_start(args, fmt);
	len = vsnprintf(head->read_buf + pos, size, fmt, args);
	va_end(args);
	if (pos + len >= head->readbuf_size)
		return false;
	head->read_avail += len;
	return true;
}

/**
 * tomoyo_get_exe - Get tomoyo_realpath() of current process.
 *
 * Returns the tomoyo_realpath() of current process on success, NULL otherwise.
 *
 * This function uses tomoyo_alloc(), so the caller must call tomoyo_free()
 * if this function didn't return NULL.
 */
static const char *tomoyo_get_exe(void)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	const char *cp = NULL;

	if (!mm)
		return NULL;
	down_read(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if ((vma->vm_flags & VM_EXECUTABLE) && vma->vm_file) {
			cp = tomoyo_realpath_from_path(&vma->vm_file->f_path);
			break;
		}
	}
	up_read(&mm->mmap_sem);
	return cp;
}

/**
 * tomoyo_get_msg - Get warning message.
 *
 * @is_enforce: Is it enforcing mode?
 *
 * Returns "ERROR" or "WARNING".
 */
const char *tomoyo_get_msg(const bool is_enforce)
{
	if (is_enforce)
		return "ERROR";
	else
		return "WARNING";
}

/**
 * tomoyo_check_flags - Check mode for specified functionality.
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 * @index:  The functionality to check mode.
 *
 * TOMOYO checks only process context.
 * This code disables TOMOYO's enforcement in case the function is called from
 * interrupt context.
 */
unsigned int tomoyo_check_flags(const struct tomoyo_domain_info *domain,
				const u8 index)
{
	const u8 profile = domain->profile;

	if (WARN_ON(in_interrupt()))
		return 0;
	return tomoyo_policy_loaded && index < TOMOYO_MAX_CONTROL_INDEX
#if TOMOYO_MAX_PROFILES != 256
		&& profile < TOMOYO_MAX_PROFILES
#endif
		&& tomoyo_profile_ptr[profile] ?
		tomoyo_profile_ptr[profile]->value[index] : 0;
}

/**
 * tomoyo_verbose_mode - Check whether TOMOYO is verbose mode.
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 *
 * Returns true if domain policy violation warning should be printed to
 * console.
 */
bool tomoyo_verbose_mode(const struct tomoyo_domain_info *domain)
{
	return tomoyo_check_flags(domain, TOMOYO_VERBOSE) != 0;
}

/**
 * tomoyo_domain_quota_is_ok - Check for domain's quota.
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 *
 * Returns true if the domain is not exceeded quota, false otherwise.
 */
bool tomoyo_domain_quota_is_ok(struct tomoyo_domain_info * const domain)
{
	unsigned int count = 0;
	struct tomoyo_acl_info *ptr;

	if (!domain)
		return true;
	down_read(&tomoyo_domain_acl_info_list_lock);
	list_for_each_entry(ptr, &domain->acl_info_list, list) {
		if (ptr->type & TOMOYO_ACL_DELETED)
			continue;
		switch (tomoyo_acl_type2(ptr)) {
			struct tomoyo_single_path_acl_record *acl1;
			struct tomoyo_double_path_acl_record *acl2;
			u16 perm;
		case TOMOYO_TYPE_SINGLE_PATH_ACL:
			acl1 = container_of(ptr,
				    struct tomoyo_single_path_acl_record,
					    head);
			perm = acl1->perm;
			if (perm & (1 << TOMOYO_TYPE_EXECUTE_ACL))
				count++;
			if (perm &
			    ((1 << TOMOYO_TYPE_READ_ACL) |
			     (1 << TOMOYO_TYPE_WRITE_ACL)))
				count++;
			if (perm & (1 << TOMOYO_TYPE_CREATE_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_UNLINK_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_MKDIR_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_RMDIR_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_MKFIFO_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_MKSOCK_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_MKBLOCK_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_MKCHAR_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_TRUNCATE_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_SYMLINK_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_REWRITE_ACL))
				count++;
			break;
		case TOMOYO_TYPE_DOUBLE_PATH_ACL:
			acl2 = container_of(ptr,
				    struct tomoyo_double_path_acl_record,
					    head);
			perm = acl2->perm;
			if (perm & (1 << TOMOYO_TYPE_LINK_ACL))
				count++;
			if (perm & (1 << TOMOYO_TYPE_RENAME_ACL))
				count++;
			break;
		}
	}
	up_read(&tomoyo_domain_acl_info_list_lock);
	if (count < tomoyo_check_flags(domain, TOMOYO_MAX_ACCEPT_ENTRY))
		return true;
	if (!domain->quota_warned) {
		domain->quota_warned = true;
		printk(KERN_WARNING "TOMOYO-WARNING: "
		       "Domain '%s' has so many ACLs to hold. "
		       "Stopped learning mode.\n", domain->domainname->name);
	}
	return false;
}

/**
 * tomoyo_find_or_assign_new_profile - Create a new profile.
 *
 * @profile: Profile number to create.
 *
 * Returns pointer to "struct tomoyo_profile" on success, NULL otherwise.
 */
static struct tomoyo_profile *tomoyo_find_or_assign_new_profile(const unsigned
								int profile)
{
	static DEFINE_MUTEX(lock);
	struct tomoyo_profile *ptr = NULL;
	int i;

	if (profile >= TOMOYO_MAX_PROFILES)
		return NULL;
	mutex_lock(&lock);
	ptr = tomoyo_profile_ptr[profile];
	if (ptr)
		goto ok;
	ptr = tomoyo_alloc_element(sizeof(*ptr));
	if (!ptr)
		goto ok;
	for (i = 0; i < TOMOYO_MAX_CONTROL_INDEX; i++)
		ptr->value[i] = tomoyo_control_array[i].current_value;
	mb(); /* Avoid out-of-order execution. */
	tomoyo_profile_ptr[profile] = ptr;
 ok:
	mutex_unlock(&lock);
	return ptr;
}

/**
 * tomoyo_write_profile - Write to profile table.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_write_profile(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	unsigned int i;
	unsigned int value;
	char *cp;
	struct tomoyo_profile *profile;
	unsigned long num;

	cp = strchr(data, '-');
	if (cp)
		*cp = '\0';
	if (strict_strtoul(data, 10, &num))
		return -EINVAL;
	if (cp)
		data = cp + 1;
	profile = tomoyo_find_or_assign_new_profile(num);
	if (!profile)
		return -EINVAL;
	cp = strchr(data, '=');
	if (!cp)
		return -EINVAL;
	*cp = '\0';
	if (!strcmp(data, "COMMENT")) {
		profile->comment = tomoyo_save_name(cp + 1);
		return 0;
	}
	for (i = 0; i < TOMOYO_MAX_CONTROL_INDEX; i++) {
		if (strcmp(data, tomoyo_control_array[i].keyword))
			continue;
		if (sscanf(cp + 1, "%u", &value) != 1) {
			int j;
			const char **modes;
			switch (i) {
			case TOMOYO_VERBOSE:
				modes = tomoyo_mode_2;
				break;
			default:
				modes = tomoyo_mode_4;
				break;
			}
			for (j = 0; j < 4; j++) {
				if (strcmp(cp + 1, modes[j]))
					continue;
				value = j;
				break;
			}
			if (j == 4)
				return -EINVAL;
		} else if (value > tomoyo_control_array[i].max_value) {
			value = tomoyo_control_array[i].max_value;
		}
		profile->value[i] = value;
		return 0;
	}
	return -EINVAL;
}

/**
 * tomoyo_read_profile - Read from profile table.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
static int tomoyo_read_profile(struct tomoyo_io_buffer *head)
{
	static const int total = TOMOYO_MAX_CONTROL_INDEX + 1;
	int step;

	if (head->read_eof)
		return 0;
	for (step = head->read_step; step < TOMOYO_MAX_PROFILES * total;
	     step++) {
		const u8 index = step / total;
		u8 type = step % total;
		const struct tomoyo_profile *profile
			= tomoyo_profile_ptr[index];
		head->read_step = step;
		if (!profile)
			continue;
		if (!type) { /* Print profile' comment tag. */
			if (!tomoyo_io_printf(head, "%u-COMMENT=%s\n",
					      index, profile->comment ?
					      profile->comment->name : ""))
				break;
			continue;
		}
		type--;
		if (type < TOMOYO_MAX_CONTROL_INDEX) {
			const unsigned int value = profile->value[type];
			const char **modes = NULL;
			const char *keyword
				= tomoyo_control_array[type].keyword;
			switch (tomoyo_control_array[type].max_value) {
			case 3:
				modes = tomoyo_mode_4;
				break;
			case 1:
				modes = tomoyo_mode_2;
				break;
			}
			if (modes) {
				if (!tomoyo_io_printf(head, "%u-%s=%s\n", index,
						      keyword, modes[value]))
					break;
			} else {
				if (!tomoyo_io_printf(head, "%u-%s=%u\n", index,
						      keyword, value))
					break;
			}
		}
	}
	if (step == TOMOYO_MAX_PROFILES * total)
		head->read_eof = true;
	return 0;
}

/*
 * tomoyo_policy_manager_entry is a structure which is used for holding list of
 * domainnames or programs which are permitted to modify configuration via
 * /sys/kernel/security/tomoyo/ interface.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_policy_manager_list .
 *  (2) "manager" is a domainname or a program's pathname.
 *  (3) "is_domain" is a bool which is true if "manager" is a domainname, false
 *      otherwise.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_policy_manager_entry {
	struct list_head list;
	/* A path to program or a domainname. */
	const struct tomoyo_path_info *manager;
	bool is_domain;  /* True if manager is a domainname. */
	bool is_deleted; /* True if this entry is deleted. */
};

/*
 * tomoyo_policy_manager_list is used for holding list of domainnames or
 * programs which are permitted to modify configuration via
 * /sys/kernel/security/tomoyo/ interface.
 *
 * An entry is added by
 *
 * # echo '<kernel> /sbin/mingetty /bin/login /bin/bash' > \
 *                                        /sys/kernel/security/tomoyo/manager
 *  (if you want to specify by a domainname)
 *
 *  or
 *
 * # echo '/usr/lib/ccs/editpolicy' > /sys/kernel/security/tomoyo/manager
 *  (if you want to specify by a program's location)
 *
 * and is deleted by
 *
 * # echo 'delete <kernel> /sbin/mingetty /bin/login /bin/bash' > \
 *                                        /sys/kernel/security/tomoyo/manager
 *
 *  or
 *
 * # echo 'delete /usr/lib/ccs/editpolicy' > \
 *                                        /sys/kernel/security/tomoyo/manager
 *
 * and all entries are retrieved by
 *
 * # cat /sys/kernel/security/tomoyo/manager
 */
static LIST_HEAD(tomoyo_policy_manager_list);
static DECLARE_RWSEM(tomoyo_policy_manager_list_lock);

/**
 * tomoyo_update_manager_entry - Add a manager entry.
 *
 * @manager:   The path to manager or the domainnamme.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_manager_entry(const char *manager,
				       const bool is_delete)
{
	struct tomoyo_policy_manager_entry *new_entry;
	struct tomoyo_policy_manager_entry *ptr;
	const struct tomoyo_path_info *saved_manager;
	int error = -ENOMEM;
	bool is_domain = false;

	if (tomoyo_is_domain_def(manager)) {
		if (!tomoyo_is_correct_domain(manager, __func__))
			return -EINVAL;
		is_domain = true;
	} else {
		if (!tomoyo_is_correct_path(manager, 1, -1, -1, __func__))
			return -EINVAL;
	}
	saved_manager = tomoyo_save_name(manager);
	if (!saved_manager)
		return -ENOMEM;
	down_write(&tomoyo_policy_manager_list_lock);
	list_for_each_entry(ptr, &tomoyo_policy_manager_list, list) {
		if (ptr->manager != saved_manager)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		goto out;
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	new_entry = tomoyo_alloc_element(sizeof(*new_entry));
	if (!new_entry)
		goto out;
	new_entry->manager = saved_manager;
	new_entry->is_domain = is_domain;
	list_add_tail(&new_entry->list, &tomoyo_policy_manager_list);
	error = 0;
 out:
	up_write(&tomoyo_policy_manager_list_lock);
	return error;
}

/**
 * tomoyo_write_manager_policy - Write manager policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_write_manager_policy(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	bool is_delete = tomoyo_str_starts(&data, TOMOYO_KEYWORD_DELETE);

	if (!strcmp(data, "manage_by_non_root")) {
		tomoyo_manage_by_non_root = !is_delete;
		return 0;
	}
	return tomoyo_update_manager_entry(data, is_delete);
}

/**
 * tomoyo_read_manager_policy - Read manager policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
static int tomoyo_read_manager_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	if (head->read_eof)
		return 0;
	down_read(&tomoyo_policy_manager_list_lock);
	list_for_each_cookie(pos, head->read_var2,
			     &tomoyo_policy_manager_list) {
		struct tomoyo_policy_manager_entry *ptr;
		ptr = list_entry(pos, struct tomoyo_policy_manager_entry,
				 list);
		if (ptr->is_deleted)
			continue;
		done = tomoyo_io_printf(head, "%s\n", ptr->manager->name);
		if (!done)
			break;
	}
	up_read(&tomoyo_policy_manager_list_lock);
	head->read_eof = done;
	return 0;
}

/**
 * tomoyo_is_policy_manager - Check whether the current process is a policy manager.
 *
 * Returns true if the current process is permitted to modify policy
 * via /sys/kernel/security/tomoyo/ interface.
 */
static bool tomoyo_is_policy_manager(void)
{
	struct tomoyo_policy_manager_entry *ptr;
	const char *exe;
	const struct task_struct *task = current;
	const struct tomoyo_path_info *domainname = tomoyo_domain()->domainname;
	bool found = false;

	if (!tomoyo_policy_loaded)
		return true;
	if (!tomoyo_manage_by_non_root && (task->cred->uid || task->cred->euid))
		return false;
	down_read(&tomoyo_policy_manager_list_lock);
	list_for_each_entry(ptr, &tomoyo_policy_manager_list, list) {
		if (!ptr->is_deleted && ptr->is_domain
		    && !tomoyo_pathcmp(domainname, ptr->manager)) {
			found = true;
			break;
		}
	}
	up_read(&tomoyo_policy_manager_list_lock);
	if (found)
		return true;
	exe = tomoyo_get_exe();
	if (!exe)
		return false;
	down_read(&tomoyo_policy_manager_list_lock);
	list_for_each_entry(ptr, &tomoyo_policy_manager_list, list) {
		if (!ptr->is_deleted && !ptr->is_domain
		    && !strcmp(exe, ptr->manager->name)) {
			found = true;
			break;
		}
	}
	up_read(&tomoyo_policy_manager_list_lock);
	if (!found) { /* Reduce error messages. */
		static pid_t last_pid;
		const pid_t pid = current->pid;
		if (last_pid != pid) {
			printk(KERN_WARNING "%s ( %s ) is not permitted to "
			       "update policies.\n", domainname->name, exe);
			last_pid = pid;
		}
	}
	tomoyo_free(exe);
	return found;
}

/**
 * tomoyo_is_select_one - Parse select command.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @data: String to parse.
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_is_select_one(struct tomoyo_io_buffer *head,
				 const char *data)
{
	unsigned int pid;
	struct tomoyo_domain_info *domain = NULL;

	if (sscanf(data, "pid=%u", &pid) == 1) {
		struct task_struct *p;
		read_lock(&tasklist_lock);
		p = find_task_by_vpid(pid);
		if (p)
			domain = tomoyo_real_domain(p);
		read_unlock(&tasklist_lock);
	} else if (!strncmp(data, "domain=", 7)) {
		if (tomoyo_is_domain_def(data + 7)) {
			down_read(&tomoyo_domain_list_lock);
			domain = tomoyo_find_domain(data + 7);
			up_read(&tomoyo_domain_list_lock);
		}
	} else
		return false;
	head->write_var1 = domain;
	/* Accessing read_buf is safe because head->io_sem is held. */
	if (!head->read_buf)
		return true; /* Do nothing if open(O_WRONLY). */
	head->read_avail = 0;
	tomoyo_io_printf(head, "# select %s\n", data);
	head->read_single_domain = true;
	head->read_eof = !domain;
	if (domain) {
		struct tomoyo_domain_info *d;
		head->read_var1 = NULL;
		down_read(&tomoyo_domain_list_lock);
		list_for_each_entry(d, &tomoyo_domain_list, list) {
			if (d == domain)
				break;
			head->read_var1 = &d->list;
		}
		up_read(&tomoyo_domain_list_lock);
		head->read_var2 = NULL;
		head->read_bit = 0;
		head->read_step = 0;
		if (domain->is_deleted)
			tomoyo_io_printf(head, "# This is a deleted domain.\n");
	}
	return true;
}

/**
 * tomoyo_delete_domain - Delete a domain.
 *
 * @domainname: The name of domain.
 *
 * Returns 0.
 */
static int tomoyo_delete_domain(char *domainname)
{
	struct tomoyo_domain_info *domain;
	struct tomoyo_path_info name;

	name.name = domainname;
	tomoyo_fill_path_info(&name);
	down_write(&tomoyo_domain_list_lock);
	/* Is there an active domain? */
	list_for_each_entry(domain, &tomoyo_domain_list, list) {
		/* Never delete tomoyo_kernel_domain */
		if (domain == &tomoyo_kernel_domain)
			continue;
		if (domain->is_deleted ||
		    tomoyo_pathcmp(domain->domainname, &name))
			continue;
		domain->is_deleted = true;
		break;
	}
	up_write(&tomoyo_domain_list_lock);
	return 0;
}

/**
 * tomoyo_write_domain_policy - Write domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_write_domain_policy(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	struct tomoyo_domain_info *domain = head->write_var1;
	bool is_delete = false;
	bool is_select = false;
	unsigned int profile;

	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_DELETE))
		is_delete = true;
	else if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_SELECT))
		is_select = true;
	if (is_select && tomoyo_is_select_one(head, data))
		return 0;
	/* Don't allow updating policies by non manager programs. */
	if (!tomoyo_is_policy_manager())
		return -EPERM;
	if (tomoyo_is_domain_def(data)) {
		domain = NULL;
		if (is_delete)
			tomoyo_delete_domain(data);
		else if (is_select) {
			down_read(&tomoyo_domain_list_lock);
			domain = tomoyo_find_domain(data);
			up_read(&tomoyo_domain_list_lock);
		} else
			domain = tomoyo_find_or_assign_new_domain(data, 0);
		head->write_var1 = domain;
		return 0;
	}
	if (!domain)
		return -EINVAL;

	if (sscanf(data, TOMOYO_KEYWORD_USE_PROFILE "%u", &profile) == 1
	    && profile < TOMOYO_MAX_PROFILES) {
		if (tomoyo_profile_ptr[profile] || !tomoyo_policy_loaded)
			domain->profile = (u8) profile;
		return 0;
	}
	if (!strcmp(data, TOMOYO_KEYWORD_IGNORE_GLOBAL_ALLOW_READ)) {
		tomoyo_set_domain_flag(domain, is_delete,
			       TOMOYO_DOMAIN_FLAGS_IGNORE_GLOBAL_ALLOW_READ);
		return 0;
	}
	return tomoyo_write_file_policy(data, domain, is_delete);
}

/**
 * tomoyo_print_single_path_acl - Print a single path ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_single_path_acl_record".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_single_path_acl(struct tomoyo_io_buffer *head,
					 struct tomoyo_single_path_acl_record *
					 ptr)
{
	int pos;
	u8 bit;
	const char *filename;
	const u16 perm = ptr->perm;

	filename = ptr->filename->name;
	for (bit = head->read_bit; bit < TOMOYO_MAX_SINGLE_PATH_OPERATION;
	     bit++) {
		const char *msg;
		if (!(perm & (1 << bit)))
			continue;
		/* Print "read/write" instead of "read" and "write". */
		if ((bit == TOMOYO_TYPE_READ_ACL ||
		     bit == TOMOYO_TYPE_WRITE_ACL)
		    && (perm & (1 << TOMOYO_TYPE_READ_WRITE_ACL)))
			continue;
		msg = tomoyo_sp2keyword(bit);
		pos = head->read_avail;
		if (!tomoyo_io_printf(head, "allow_%s %s\n", msg, filename))
			goto out;
	}
	head->read_bit = 0;
	return true;
 out:
	head->read_bit = bit;
	head->read_avail = pos;
	return false;
}

/**
 * tomoyo_print_double_path_acl - Print a double path ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_double_path_acl_record".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_double_path_acl(struct tomoyo_io_buffer *head,
					 struct tomoyo_double_path_acl_record *
					 ptr)
{
	int pos;
	const char *filename1;
	const char *filename2;
	const u8 perm = ptr->perm;
	u8 bit;

	filename1 = ptr->filename1->name;
	filename2 = ptr->filename2->name;
	for (bit = head->read_bit; bit < TOMOYO_MAX_DOUBLE_PATH_OPERATION;
	     bit++) {
		const char *msg;
		if (!(perm & (1 << bit)))
			continue;
		msg = tomoyo_dp2keyword(bit);
		pos = head->read_avail;
		if (!tomoyo_io_printf(head, "allow_%s %s %s\n", msg,
				      filename1, filename2))
			goto out;
	}
	head->read_bit = 0;
	return true;
 out:
	head->read_bit = bit;
	head->read_avail = pos;
	return false;
}

/**
 * tomoyo_print_entry - Print an ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to an ACL entry.
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_entry(struct tomoyo_io_buffer *head,
			       struct tomoyo_acl_info *ptr)
{
	const u8 acl_type = tomoyo_acl_type2(ptr);

	if (acl_type & TOMOYO_ACL_DELETED)
		return true;
	if (acl_type == TOMOYO_TYPE_SINGLE_PATH_ACL) {
		struct tomoyo_single_path_acl_record *acl
			= container_of(ptr,
				       struct tomoyo_single_path_acl_record,
				       head);
		return tomoyo_print_single_path_acl(head, acl);
	}
	if (acl_type == TOMOYO_TYPE_DOUBLE_PATH_ACL) {
		struct tomoyo_double_path_acl_record *acl
			= container_of(ptr,
				       struct tomoyo_double_path_acl_record,
				       head);
		return tomoyo_print_double_path_acl(head, acl);
	}
	BUG(); /* This must not happen. */
	return false;
}

/**
 * tomoyo_read_domain_policy - Read domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
static int tomoyo_read_domain_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *dpos;
	struct list_head *apos;
	bool done = true;

	if (head->read_eof)
		return 0;
	if (head->read_step == 0)
		head->read_step = 1;
	down_read(&tomoyo_domain_list_lock);
	list_for_each_cookie(dpos, head->read_var1, &tomoyo_domain_list) {
		struct tomoyo_domain_info *domain;
		const char *quota_exceeded = "";
		const char *transition_failed = "";
		const char *ignore_global_allow_read = "";
		domain = list_entry(dpos, struct tomoyo_domain_info, list);
		if (head->read_step != 1)
			goto acl_loop;
		if (domain->is_deleted && !head->read_single_domain)
			continue;
		/* Print domainname and flags. */
		if (domain->quota_warned)
			quota_exceeded = "quota_exceeded\n";
		if (domain->flags & TOMOYO_DOMAIN_FLAGS_TRANSITION_FAILED)
			transition_failed = "transition_failed\n";
		if (domain->flags &
		    TOMOYO_DOMAIN_FLAGS_IGNORE_GLOBAL_ALLOW_READ)
			ignore_global_allow_read
				= TOMOYO_KEYWORD_IGNORE_GLOBAL_ALLOW_READ "\n";
		done = tomoyo_io_printf(head, "%s\n" TOMOYO_KEYWORD_USE_PROFILE
					"%u\n%s%s%s\n",
					domain->domainname->name,
					domain->profile, quota_exceeded,
					transition_failed,
					ignore_global_allow_read);
		if (!done)
			break;
		head->read_step = 2;
acl_loop:
		if (head->read_step == 3)
			goto tail_mark;
		/* Print ACL entries in the domain. */
		down_read(&tomoyo_domain_acl_info_list_lock);
		list_for_each_cookie(apos, head->read_var2,
				     &domain->acl_info_list) {
			struct tomoyo_acl_info *ptr
				= list_entry(apos, struct tomoyo_acl_info,
					     list);
			done = tomoyo_print_entry(head, ptr);
			if (!done)
				break;
		}
		up_read(&tomoyo_domain_acl_info_list_lock);
		if (!done)
			break;
		head->read_step = 3;
tail_mark:
		done = tomoyo_io_printf(head, "\n");
		if (!done)
			break;
		head->read_step = 1;
		if (head->read_single_domain)
			break;
	}
	up_read(&tomoyo_domain_list_lock);
	head->read_eof = done;
	return 0;
}

/**
 * tomoyo_write_domain_profile - Assign profile for specified domain.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, -EINVAL otherwise.
 *
 * This is equivalent to doing
 *
 *     ( echo "select " $domainname; echo "use_profile " $profile ) |
 *     /usr/lib/ccs/loadpolicy -d
 */
static int tomoyo_write_domain_profile(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	char *cp = strchr(data, ' ');
	struct tomoyo_domain_info *domain;
	unsigned long profile;

	if (!cp)
		return -EINVAL;
	*cp = '\0';
	down_read(&tomoyo_domain_list_lock);
	domain = tomoyo_find_domain(cp + 1);
	up_read(&tomoyo_domain_list_lock);
	if (strict_strtoul(data, 10, &profile))
		return -EINVAL;
	if (domain && profile < TOMOYO_MAX_PROFILES
	    && (tomoyo_profile_ptr[profile] || !tomoyo_policy_loaded))
		domain->profile = (u8) profile;
	return 0;
}

/**
 * tomoyo_read_domain_profile - Read only domainname and profile.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns list of profile number and domainname pairs.
 *
 * This is equivalent to doing
 *
 *     grep -A 1 '^<kernel>' /sys/kernel/security/tomoyo/domain_policy |
 *     awk ' { if ( domainname == "" ) { if ( $1 == "<kernel>" )
 *     domainname = $0; } else if ( $1 == "use_profile" ) {
 *     print $2 " " domainname; domainname = ""; } } ; '
 */
static int tomoyo_read_domain_profile(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	if (head->read_eof)
		return 0;
	down_read(&tomoyo_domain_list_lock);
	list_for_each_cookie(pos, head->read_var1, &tomoyo_domain_list) {
		struct tomoyo_domain_info *domain;
		domain = list_entry(pos, struct tomoyo_domain_info, list);
		if (domain->is_deleted)
			continue;
		done = tomoyo_io_printf(head, "%u %s\n", domain->profile,
					domain->domainname->name);
		if (!done)
			break;
	}
	up_read(&tomoyo_domain_list_lock);
	head->read_eof = done;
	return 0;
}

/**
 * tomoyo_write_pid: Specify PID to obtain domainname.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
static int tomoyo_write_pid(struct tomoyo_io_buffer *head)
{
	unsigned long pid;
	/* No error check. */
	strict_strtoul(head->write_buf, 10, &pid);
	head->read_step = (int) pid;
	head->read_eof = false;
	return 0;
}

/**
 * tomoyo_read_pid - Get domainname of the specified PID.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns the domainname which the specified PID is in on success,
 * empty string otherwise.
 * The PID is specified by tomoyo_write_pid() so that the user can obtain
 * using read()/write() interface rather than sysctl() interface.
 */
static int tomoyo_read_pid(struct tomoyo_io_buffer *head)
{
	if (head->read_avail == 0 && !head->read_eof) {
		const int pid = head->read_step;
		struct task_struct *p;
		struct tomoyo_domain_info *domain = NULL;
		read_lock(&tasklist_lock);
		p = find_task_by_vpid(pid);
		if (p)
			domain = tomoyo_real_domain(p);
		read_unlock(&tasklist_lock);
		if (domain)
			tomoyo_io_printf(head, "%d %u %s", pid, domain->profile,
					 domain->domainname->name);
		head->read_eof = true;
	}
	return 0;
}

/**
 * tomoyo_write_exception_policy - Write exception policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_write_exception_policy(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	bool is_delete = tomoyo_str_starts(&data, TOMOYO_KEYWORD_DELETE);

	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_KEEP_DOMAIN))
		return tomoyo_write_domain_keeper_policy(data, false,
							 is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_NO_KEEP_DOMAIN))
		return tomoyo_write_domain_keeper_policy(data, true, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_INITIALIZE_DOMAIN))
		return tomoyo_write_domain_initializer_policy(data, false,
							      is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_NO_INITIALIZE_DOMAIN))
		return tomoyo_write_domain_initializer_policy(data, true,
							      is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_ALIAS))
		return tomoyo_write_alias_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_ALLOW_READ))
		return tomoyo_write_globally_readable_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_FILE_PATTERN))
		return tomoyo_write_pattern_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_DENY_REWRITE))
		return tomoyo_write_no_rewrite_policy(data, is_delete);
	return -EINVAL;
}

/**
 * tomoyo_read_exception_policy - Read exception policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, -EINVAL otherwise.
 */
static int tomoyo_read_exception_policy(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		switch (head->read_step) {
		case 0:
			head->read_var2 = NULL;
			head->read_step = 1;
		case 1:
			if (!tomoyo_read_domain_keeper_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 2;
		case 2:
			if (!tomoyo_read_globally_readable_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 3;
		case 3:
			head->read_var2 = NULL;
			head->read_step = 4;
		case 4:
			if (!tomoyo_read_domain_initializer_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 5;
		case 5:
			if (!tomoyo_read_alias_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 6;
		case 6:
			head->read_var2 = NULL;
			head->read_step = 7;
		case 7:
			if (!tomoyo_read_file_pattern(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 8;
		case 8:
			if (!tomoyo_read_no_rewrite_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 9;
		case 9:
			head->read_eof = true;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

/* path to policy loader */
static const char *tomoyo_loader = "/sbin/tomoyo-init";

/**
 * tomoyo_policy_loader_exists - Check whether /sbin/tomoyo-init exists.
 *
 * Returns true if /sbin/tomoyo-init exists, false otherwise.
 */
static bool tomoyo_policy_loader_exists(void)
{
	/*
	 * Don't activate MAC if the policy loader doesn't exist.
	 * If the initrd includes /sbin/init but real-root-dev has not
	 * mounted on / yet, activating MAC will block the system since
	 * policies are not loaded yet.
	 * Thus, let do_execve() call this function everytime.
	 */
	struct path path;

	if (kern_path(tomoyo_loader, LOOKUP_FOLLOW, &path)) {
		printk(KERN_INFO "Not activating Mandatory Access Control now "
		       "since %s doesn't exist.\n", tomoyo_loader);
		return false;
	}
	path_put(&path);
	return true;
}

/**
 * tomoyo_load_policy - Run external policy loader to load policy.
 *
 * @filename: The program about to start.
 *
 * This function checks whether @filename is /sbin/init , and if so
 * invoke /sbin/tomoyo-init and wait for the termination of /sbin/tomoyo-init
 * and then continues invocation of /sbin/init.
 * /sbin/tomoyo-init reads policy files in /etc/tomoyo/ directory and
 * writes to /sys/kernel/security/tomoyo/ interfaces.
 *
 * Returns nothing.
 */
void tomoyo_load_policy(const char *filename)
{
	char *argv[2];
	char *envp[3];

	if (tomoyo_policy_loaded)
		return;
	/*
	 * Check filename is /sbin/init or /sbin/tomoyo-start.
	 * /sbin/tomoyo-start is a dummy filename in case where /sbin/init can't
	 * be passed.
	 * You can create /sbin/tomoyo-start by
	 * "ln -s /bin/true /sbin/tomoyo-start".
	 */
	if (strcmp(filename, "/sbin/init") &&
	    strcmp(filename, "/sbin/tomoyo-start"))
		return;
	if (!tomoyo_policy_loader_exists())
		return;

	printk(KERN_INFO "Calling %s to load policy. Please wait.\n",
	       tomoyo_loader);
	argv[0] = (char *) tomoyo_loader;
	argv[1] = NULL;
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[2] = NULL;
	call_usermodehelper(argv[0], argv, envp, 1);

	printk(KERN_INFO "TOMOYO: 2.2.0   2009/04/01\n");
	printk(KERN_INFO "Mandatory Access Control activated.\n");
	tomoyo_policy_loaded = true;
	{ /* Check all profiles currently assigned to domains are defined. */
		struct tomoyo_domain_info *domain;
		down_read(&tomoyo_domain_list_lock);
		list_for_each_entry(domain, &tomoyo_domain_list, list) {
			const u8 profile = domain->profile;
			if (tomoyo_profile_ptr[profile])
				continue;
			panic("Profile %u (used by '%s') not defined.\n",
			      profile, domain->domainname->name);
		}
		up_read(&tomoyo_domain_list_lock);
	}
}

/**
 * tomoyo_read_version: Get version.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns version information.
 */
static int tomoyo_read_version(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		tomoyo_io_printf(head, "2.2.0");
		head->read_eof = true;
	}
	return 0;
}

/**
 * tomoyo_read_self_domain - Get the current process's domainname.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns the current process's domainname.
 */
static int tomoyo_read_self_domain(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		/*
		 * tomoyo_domain()->domainname != NULL
		 * because every process belongs to a domain and
		 * the domain's name cannot be NULL.
		 */
		tomoyo_io_printf(head, "%s", tomoyo_domain()->domainname->name);
		head->read_eof = true;
	}
	return 0;
}

/**
 * tomoyo_open_control - open() for /sys/kernel/security/tomoyo/ interface.
 *
 * @type: Type of interface.
 * @file: Pointer to "struct file".
 *
 * Associates policy handler and returns 0 on success, -ENOMEM otherwise.
 */
static int tomoyo_open_control(const u8 type, struct file *file)
{
	struct tomoyo_io_buffer *head = tomoyo_alloc(sizeof(*head));

	if (!head)
		return -ENOMEM;
	mutex_init(&head->io_sem);
	switch (type) {
	case TOMOYO_DOMAINPOLICY:
		/* /sys/kernel/security/tomoyo/domain_policy */
		head->write = tomoyo_write_domain_policy;
		head->read = tomoyo_read_domain_policy;
		break;
	case TOMOYO_EXCEPTIONPOLICY:
		/* /sys/kernel/security/tomoyo/exception_policy */
		head->write = tomoyo_write_exception_policy;
		head->read = tomoyo_read_exception_policy;
		break;
	case TOMOYO_SELFDOMAIN:
		/* /sys/kernel/security/tomoyo/self_domain */
		head->read = tomoyo_read_self_domain;
		break;
	case TOMOYO_DOMAIN_STATUS:
		/* /sys/kernel/security/tomoyo/.domain_status */
		head->write = tomoyo_write_domain_profile;
		head->read = tomoyo_read_domain_profile;
		break;
	case TOMOYO_PROCESS_STATUS:
		/* /sys/kernel/security/tomoyo/.process_status */
		head->write = tomoyo_write_pid;
		head->read = tomoyo_read_pid;
		break;
	case TOMOYO_VERSION:
		/* /sys/kernel/security/tomoyo/version */
		head->read = tomoyo_read_version;
		head->readbuf_size = 128;
		break;
	case TOMOYO_MEMINFO:
		/* /sys/kernel/security/tomoyo/meminfo */
		head->write = tomoyo_write_memory_quota;
		head->read = tomoyo_read_memory_counter;
		head->readbuf_size = 512;
		break;
	case TOMOYO_PROFILE:
		/* /sys/kernel/security/tomoyo/profile */
		head->write = tomoyo_write_profile;
		head->read = tomoyo_read_profile;
		break;
	case TOMOYO_MANAGER:
		/* /sys/kernel/security/tomoyo/manager */
		head->write = tomoyo_write_manager_policy;
		head->read = tomoyo_read_manager_policy;
		break;
	}
	if (!(file->f_mode & FMODE_READ)) {
		/*
		 * No need to allocate read_buf since it is not opened
		 * for reading.
		 */
		head->read = NULL;
	} else {
		if (!head->readbuf_size)
			head->readbuf_size = 4096 * 2;
		head->read_buf = tomoyo_alloc(head->readbuf_size);
		if (!head->read_buf) {
			tomoyo_free(head);
			return -ENOMEM;
		}
	}
	if (!(file->f_mode & FMODE_WRITE)) {
		/*
		 * No need to allocate write_buf since it is not opened
		 * for writing.
		 */
		head->write = NULL;
	} else if (head->write) {
		head->writebuf_size = 4096 * 2;
		head->write_buf = tomoyo_alloc(head->writebuf_size);
		if (!head->write_buf) {
			tomoyo_free(head->read_buf);
			tomoyo_free(head);
			return -ENOMEM;
		}
	}
	file->private_data = head;
	/*
	 * Call the handler now if the file is
	 * /sys/kernel/security/tomoyo/self_domain
	 * so that the user can use
	 * cat < /sys/kernel/security/tomoyo/self_domain"
	 * to know the current process's domainname.
	 */
	if (type == TOMOYO_SELFDOMAIN)
		tomoyo_read_control(file, NULL, 0);
	return 0;
}

/**
 * tomoyo_read_control - read() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file:       Pointer to "struct file".
 * @buffer:     Poiner to buffer to write to.
 * @buffer_len: Size of @buffer.
 *
 * Returns bytes read on success, negative value otherwise.
 */
static int tomoyo_read_control(struct file *file, char __user *buffer,
			       const int buffer_len)
{
	int len = 0;
	struct tomoyo_io_buffer *head = file->private_data;
	char *cp;

	if (!head->read)
		return -ENOSYS;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	/* Call the policy handler. */
	len = head->read(head);
	if (len < 0)
		goto out;
	/* Write to buffer. */
	len = head->read_avail;
	if (len > buffer_len)
		len = buffer_len;
	if (!len)
		goto out;
	/* head->read_buf changes by some functions. */
	cp = head->read_buf;
	if (copy_to_user(buffer, cp, len)) {
		len = -EFAULT;
		goto out;
	}
	head->read_avail -= len;
	memmove(cp, cp + len, head->read_avail);
 out:
	mutex_unlock(&head->io_sem);
	return len;
}

/**
 * tomoyo_write_control - write() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file:       Pointer to "struct file".
 * @buffer:     Pointer to buffer to read from.
 * @buffer_len: Size of @buffer.
 *
 * Returns @buffer_len on success, negative value otherwise.
 */
static int tomoyo_write_control(struct file *file, const char __user *buffer,
				const int buffer_len)
{
	struct tomoyo_io_buffer *head = file->private_data;
	int error = buffer_len;
	int avail_len = buffer_len;
	char *cp0 = head->write_buf;

	if (!head->write)
		return -ENOSYS;
	if (!access_ok(VERIFY_READ, buffer, buffer_len))
		return -EFAULT;
	/* Don't allow updating policies by non manager programs. */
	if (head->write != tomoyo_write_pid &&
	    head->write != tomoyo_write_domain_policy &&
	    !tomoyo_is_policy_manager())
		return -EPERM;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	/* Read a line and dispatch it to the policy handler. */
	while (avail_len > 0) {
		char c;
		if (head->write_avail >= head->writebuf_size - 1) {
			error = -ENOMEM;
			break;
		} else if (get_user(c, buffer)) {
			error = -EFAULT;
			break;
		}
		buffer++;
		avail_len--;
		cp0[head->write_avail++] = c;
		if (c != '\n')
			continue;
		cp0[head->write_avail - 1] = '\0';
		head->write_avail = 0;
		tomoyo_normalize_line(cp0);
		head->write(head);
	}
	mutex_unlock(&head->io_sem);
	return error;
}

/**
 * tomoyo_close_control - close() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file: Pointer to "struct file".
 *
 * Releases memory and returns 0.
 */
static int tomoyo_close_control(struct file *file)
{
	struct tomoyo_io_buffer *head = file->private_data;

	/* Release memory used for policy I/O. */
	tomoyo_free(head->read_buf);
	head->read_buf = NULL;
	tomoyo_free(head->write_buf);
	head->write_buf = NULL;
	tomoyo_free(head);
	head = NULL;
	file->private_data = NULL;
	return 0;
}

/**
 * tomoyo_alloc_acl_element - Allocate permanent memory for ACL entry.
 *
 * @acl_type:  Type of ACL entry.
 *
 * Returns pointer to the ACL entry on success, NULL otherwise.
 */
void *tomoyo_alloc_acl_element(const u8 acl_type)
{
	int len;
	struct tomoyo_acl_info *ptr;

	switch (acl_type) {
	case TOMOYO_TYPE_SINGLE_PATH_ACL:
		len = sizeof(struct tomoyo_single_path_acl_record);
		break;
	case TOMOYO_TYPE_DOUBLE_PATH_ACL:
		len = sizeof(struct tomoyo_double_path_acl_record);
		break;
	default:
		return NULL;
	}
	ptr = tomoyo_alloc_element(len);
	if (!ptr)
		return NULL;
	ptr->type = acl_type;
	return ptr;
}

/**
 * tomoyo_open - open() for /sys/kernel/security/tomoyo/ interface.
 *
 * @inode: Pointer to "struct inode".
 * @file:  Pointer to "struct file".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_open(struct inode *inode, struct file *file)
{
	const int key = ((u8 *) file->f_path.dentry->d_inode->i_private)
		- ((u8 *) NULL);
	return tomoyo_open_control(key, file);
}

/**
 * tomoyo_release - close() for /sys/kernel/security/tomoyo/ interface.
 *
 * @inode: Pointer to "struct inode".
 * @file:  Pointer to "struct file".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_release(struct inode *inode, struct file *file)
{
	return tomoyo_close_control(file);
}

/**
 * tomoyo_read - read() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file:  Pointer to "struct file".
 * @buf:   Pointer to buffer.
 * @count: Size of @buf.
 * @ppos:  Unused.
 *
 * Returns bytes read on success, negative value otherwise.
 */
static ssize_t tomoyo_read(struct file *file, char __user *buf, size_t count,
			   loff_t *ppos)
{
	return tomoyo_read_control(file, buf, count);
}

/**
 * tomoyo_write - write() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file:  Pointer to "struct file".
 * @buf:   Pointer to buffer.
 * @count: Size of @buf.
 * @ppos:  Unused.
 *
 * Returns @count on success, negative value otherwise.
 */
static ssize_t tomoyo_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	return tomoyo_write_control(file, buf, count);
}

/*
 * tomoyo_operations is a "struct file_operations" which is used for handling
 * /sys/kernel/security/tomoyo/ interface.
 *
 * Some files under /sys/kernel/security/tomoyo/ directory accept open(O_RDWR).
 * See tomoyo_io_buffer for internals.
 */
static const struct file_operations tomoyo_operations = {
	.open    = tomoyo_open,
	.release = tomoyo_release,
	.read    = tomoyo_read,
	.write   = tomoyo_write,
};

/**
 * tomoyo_create_entry - Create interface files under /sys/kernel/security/tomoyo/ directory.
 *
 * @name:   The name of the interface file.
 * @mode:   The permission of the interface file.
 * @parent: The parent directory.
 * @key:    Type of interface.
 *
 * Returns nothing.
 */
static void __init tomoyo_create_entry(const char *name, const mode_t mode,
				       struct dentry *parent, const u8 key)
{
	securityfs_create_file(name, mode, parent, ((u8 *) NULL) + key,
			       &tomoyo_operations);
}

/**
 * tomoyo_initerface_init - Initialize /sys/kernel/security/tomoyo/ interface.
 *
 * Returns 0.
 */
static int __init tomoyo_initerface_init(void)
{
	struct dentry *tomoyo_dir;

	/* Don't create securityfs entries unless registered. */
	if (current_cred()->security != &tomoyo_kernel_domain)
		return 0;

	tomoyo_dir = securityfs_create_dir("tomoyo", NULL);
	tomoyo_create_entry("domain_policy",    0600, tomoyo_dir,
			    TOMOYO_DOMAINPOLICY);
	tomoyo_create_entry("exception_policy", 0600, tomoyo_dir,
			    TOMOYO_EXCEPTIONPOLICY);
	tomoyo_create_entry("self_domain",      0400, tomoyo_dir,
			    TOMOYO_SELFDOMAIN);
	tomoyo_create_entry(".domain_status",   0600, tomoyo_dir,
			    TOMOYO_DOMAIN_STATUS);
	tomoyo_create_entry(".process_status",  0600, tomoyo_dir,
			    TOMOYO_PROCESS_STATUS);
	tomoyo_create_entry("meminfo",          0600, tomoyo_dir,
			    TOMOYO_MEMINFO);
	tomoyo_create_entry("profile",          0600, tomoyo_dir,
			    TOMOYO_PROFILE);
	tomoyo_create_entry("manager",          0600, tomoyo_dir,
			    TOMOYO_MANAGER);
	tomoyo_create_entry("version",          0400, tomoyo_dir,
			    TOMOYO_VERSION);
	return 0;
}

fs_initcall(tomoyo_initerface_init);
