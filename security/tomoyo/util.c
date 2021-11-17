// SPDX-License-Identifier: GPL-2.0
/*
 * security/tomoyo/util.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/slab.h>
#include <linux/rculist.h>

#include "common.h"

/* Lock for protecting policy. */
DEFINE_MUTEX(tomoyo_policy_lock);

/* Has /sbin/init started? */
bool tomoyo_policy_loaded;

/*
 * Mapping table from "enum tomoyo_mac_index" to
 * "enum tomoyo_mac_category_index".
 */
const u8 tomoyo_index2category[TOMOYO_MAX_MAC_INDEX] = {
	/* CONFIG::file group */
	[TOMOYO_MAC_FILE_EXECUTE]    = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_OPEN]       = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_CREATE]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_UNLINK]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_GETATTR]    = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_MKDIR]      = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_RMDIR]      = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_MKFIFO]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_MKSOCK]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_TRUNCATE]   = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_SYMLINK]    = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_MKBLOCK]    = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_MKCHAR]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_LINK]       = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_RENAME]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_CHMOD]      = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_CHOWN]      = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_CHGRP]      = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_IOCTL]      = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_CHROOT]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_MOUNT]      = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_UMOUNT]     = TOMOYO_MAC_CATEGORY_FILE,
	[TOMOYO_MAC_FILE_PIVOT_ROOT] = TOMOYO_MAC_CATEGORY_FILE,
	/* CONFIG::network group */
	[TOMOYO_MAC_NETWORK_INET_STREAM_BIND]       =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_INET_STREAM_LISTEN]     =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_INET_STREAM_CONNECT]    =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_INET_DGRAM_BIND]        =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_INET_DGRAM_SEND]        =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_INET_RAW_BIND]          =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_INET_RAW_SEND]          =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_STREAM_BIND]       =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_STREAM_LISTEN]     =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_STREAM_CONNECT]    =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_DGRAM_BIND]        =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_DGRAM_SEND]        =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_BIND]    =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_LISTEN]  =
	TOMOYO_MAC_CATEGORY_NETWORK,
	[TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_CONNECT] =
	TOMOYO_MAC_CATEGORY_NETWORK,
	/* CONFIG::misc group */
	[TOMOYO_MAC_ENVIRON]         = TOMOYO_MAC_CATEGORY_MISC,
};

/**
 * tomoyo_convert_time - Convert time_t to YYYY/MM/DD hh/mm/ss.
 *
 * @time64: Seconds since 1970/01/01 00:00:00.
 * @stamp:  Pointer to "struct tomoyo_time".
 *
 * Returns nothing.
 */
void tomoyo_convert_time(time64_t time64, struct tomoyo_time *stamp)
{
	struct tm tm;

	time64_to_tm(time64, 0, &tm);
	stamp->sec = tm.tm_sec;
	stamp->min = tm.tm_min;
	stamp->hour = tm.tm_hour;
	stamp->day = tm.tm_mday;
	stamp->month = tm.tm_mon + 1;
	stamp->year = tm.tm_year + 1900;
}

/**
 * tomoyo_permstr - Find permission keywords.
 *
 * @string: String representation for permissions in foo/bar/buz format.
 * @keyword: Keyword to find from @string/
 *
 * Returns true if @keyword was found in @string, false otherwise.
 *
 * This function assumes that strncmp(w1, w2, strlen(w1)) != 0 if w1 != w2.
 */
bool tomoyo_permstr(const char *string, const char *keyword)
{
	const char *cp = strstr(string, keyword);

	if (cp)
		return cp == string || *(cp - 1) == '/';
	return false;
}

/**
 * tomoyo_read_token - Read a word from a line.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 *
 * Returns a word on success, "" otherwise.
 *
 * To allow the caller to skip NULL check, this function returns "" rather than
 * NULL if there is no more words to read.
 */
char *tomoyo_read_token(struct tomoyo_acl_param *param)
{
	char *pos = param->data;
	char *del = strchr(pos, ' ');

	if (del)
		*del++ = '\0';
	else
		del = pos + strlen(pos);
	param->data = del;
	return pos;
}

static bool tomoyo_correct_path2(const char *filename, const size_t len);

/**
 * tomoyo_get_domainname - Read a domainname from a line.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 *
 * Returns a domainname on success, NULL otherwise.
 */
const struct tomoyo_path_info *tomoyo_get_domainname
(struct tomoyo_acl_param *param)
{
	char *start = param->data;
	char *pos = start;

	while (*pos) {
		if (*pos++ != ' ' ||
		    tomoyo_correct_path2(pos, strchrnul(pos, ' ') - pos))
			continue;
		*(pos - 1) = '\0';
		break;
	}
	param->data = pos;
	if (tomoyo_correct_domain(start))
		return tomoyo_get_name(start);
	return NULL;
}

/**
 * tomoyo_parse_ulong - Parse an "unsigned long" value.
 *
 * @result: Pointer to "unsigned long".
 * @str:    Pointer to string to parse.
 *
 * Returns one of values in "enum tomoyo_value_type".
 *
 * The @src is updated to point the first character after the value
 * on success.
 */
u8 tomoyo_parse_ulong(unsigned long *result, char **str)
{
	const char *cp = *str;
	char *ep;
	int base = 10;

	if (*cp == '0') {
		char c = *(cp + 1);

		if (c == 'x' || c == 'X') {
			base = 16;
			cp += 2;
		} else if (c >= '0' && c <= '7') {
			base = 8;
			cp++;
		}
	}
	*result = simple_strtoul(cp, &ep, base);
	if (cp == ep)
		return TOMOYO_VALUE_TYPE_INVALID;
	*str = ep;
	switch (base) {
	case 16:
		return TOMOYO_VALUE_TYPE_HEXADECIMAL;
	case 8:
		return TOMOYO_VALUE_TYPE_OCTAL;
	default:
		return TOMOYO_VALUE_TYPE_DECIMAL;
	}
}

/**
 * tomoyo_print_ulong - Print an "unsigned long" value.
 *
 * @buffer:     Pointer to buffer.
 * @buffer_len: Size of @buffer.
 * @value:      An "unsigned long" value.
 * @type:       Type of @value.
 *
 * Returns nothing.
 */
void tomoyo_print_ulong(char *buffer, const int buffer_len,
			const unsigned long value, const u8 type)
{
	if (type == TOMOYO_VALUE_TYPE_DECIMAL)
		snprintf(buffer, buffer_len, "%lu", value);
	else if (type == TOMOYO_VALUE_TYPE_OCTAL)
		snprintf(buffer, buffer_len, "0%lo", value);
	else if (type == TOMOYO_VALUE_TYPE_HEXADECIMAL)
		snprintf(buffer, buffer_len, "0x%lX", value);
	else
		snprintf(buffer, buffer_len, "type(%u)", type);
}

/**
 * tomoyo_parse_name_union - Parse a tomoyo_name_union.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 * @ptr:   Pointer to "struct tomoyo_name_union".
 *
 * Returns true on success, false otherwise.
 */
bool tomoyo_parse_name_union(struct tomoyo_acl_param *param,
			     struct tomoyo_name_union *ptr)
{
	char *filename;

	if (param->data[0] == '@') {
		param->data++;
		ptr->group = tomoyo_get_group(param, TOMOYO_PATH_GROUP);
		return ptr->group != NULL;
	}
	filename = tomoyo_read_token(param);
	if (!tomoyo_correct_word(filename))
		return false;
	ptr->filename = tomoyo_get_name(filename);
	return ptr->filename != NULL;
}

/**
 * tomoyo_parse_number_union - Parse a tomoyo_number_union.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 * @ptr:   Pointer to "struct tomoyo_number_union".
 *
 * Returns true on success, false otherwise.
 */
bool tomoyo_parse_number_union(struct tomoyo_acl_param *param,
			       struct tomoyo_number_union *ptr)
{
	char *data;
	u8 type;
	unsigned long v;

	memset(ptr, 0, sizeof(*ptr));
	if (param->data[0] == '@') {
		param->data++;
		ptr->group = tomoyo_get_group(param, TOMOYO_NUMBER_GROUP);
		return ptr->group != NULL;
	}
	data = tomoyo_read_token(param);
	type = tomoyo_parse_ulong(&v, &data);
	if (type == TOMOYO_VALUE_TYPE_INVALID)
		return false;
	ptr->values[0] = v;
	ptr->value_type[0] = type;
	if (!*data) {
		ptr->values[1] = v;
		ptr->value_type[1] = type;
		return true;
	}
	if (*data++ != '-')
		return false;
	type = tomoyo_parse_ulong(&v, &data);
	if (type == TOMOYO_VALUE_TYPE_INVALID || *data || ptr->values[0] > v)
		return false;
	ptr->values[1] = v;
	ptr->value_type[1] = type;
	return true;
}

/**
 * tomoyo_byte_range - Check whether the string is a \ooo style octal value.
 *
 * @str: Pointer to the string.
 *
 * Returns true if @str is a \ooo style octal value, false otherwise.
 *
 * TOMOYO uses \ooo style representation for 0x01 - 0x20 and 0x7F - 0xFF.
 * This function verifies that \ooo is in valid range.
 */
static inline bool tomoyo_byte_range(const char *str)
{
	return *str >= '0' && *str++ <= '3' &&
		*str >= '0' && *str++ <= '7' &&
		*str >= '0' && *str <= '7';
}

/**
 * tomoyo_alphabet_char - Check whether the character is an alphabet.
 *
 * @c: The character to check.
 *
 * Returns true if @c is an alphabet character, false otherwise.
 */
static inline bool tomoyo_alphabet_char(const char c)
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
 * tomoyo_valid - Check whether the character is a valid char.
 *
 * @c: The character to check.
 *
 * Returns true if @c is a valid character, false otherwise.
 */
static inline bool tomoyo_valid(const unsigned char c)
{
	return c > ' ' && c < 127;
}

/**
 * tomoyo_invalid - Check whether the character is an invalid char.
 *
 * @c: The character to check.
 *
 * Returns true if @c is an invalid character, false otherwise.
 */
static inline bool tomoyo_invalid(const unsigned char c)
{
	return c && (c <= ' ' || c >= 127);
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
bool tomoyo_str_starts(char **src, const char *find)
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
void tomoyo_normalize_line(unsigned char *buffer)
{
	unsigned char *sp = buffer;
	unsigned char *dp = buffer;
	bool first = true;

	while (tomoyo_invalid(*sp))
		sp++;
	while (*sp) {
		if (!first)
			*dp++ = ' ';
		first = false;
		while (tomoyo_valid(*sp))
			*dp++ = *sp++;
		while (tomoyo_invalid(*sp))
			sp++;
	}
	*dp = '\0';
}

/**
 * tomoyo_correct_word2 - Validate a string.
 *
 * @string: The string to check. Maybe non-'\0'-terminated.
 * @len:    Length of @string.
 *
 * Check whether the given string follows the naming rules.
 * Returns true if @string follows the naming rules, false otherwise.
 */
static bool tomoyo_correct_word2(const char *string, size_t len)
{
	u8 recursion = 20;
	const char *const start = string;
	bool in_repetition = false;

	if (!len)
		goto out;
	while (len--) {
		unsigned char c = *string++;

		if (c == '\\') {
			if (!len--)
				goto out;
			c = *string++;
			if (c >= '0' && c <= '3') {
				unsigned char d;
				unsigned char e;

				if (!len-- || !len--)
					goto out;
				d = *string++;
				e = *string++;
				if (d < '0' || d > '7' || e < '0' || e > '7')
					goto out;
				c = tomoyo_make_byte(c, d, e);
				if (c <= ' ' || c >= 127)
					continue;
				goto out;
			}
			switch (c) {
			case '\\':  /* "\\" */
			case '+':   /* "\+" */
			case '?':   /* "\?" */
			case 'x':   /* "\x" */
			case 'a':   /* "\a" */
			case '-':   /* "\-" */
				continue;
			}
			if (!recursion--)
				goto out;
			switch (c) {
			case '*':   /* "\*" */
			case '@':   /* "\@" */
			case '$':   /* "\$" */
			case 'X':   /* "\X" */
			case 'A':   /* "\A" */
				continue;
			case '{':   /* "/\{" */
				if (string - 3 < start || *(string - 3) != '/')
					goto out;
				in_repetition = true;
				continue;
			case '}':   /* "\}/" */
				if (*string != '/')
					goto out;
				if (!in_repetition)
					goto out;
				in_repetition = false;
				continue;
			}
			goto out;
		} else if (in_repetition && c == '/') {
			goto out;
		} else if (c <= ' ' || c >= 127) {
			goto out;
		}
	}
	if (in_repetition)
		goto out;
	return true;
 out:
	return false;
}

/**
 * tomoyo_correct_word - Validate a string.
 *
 * @string: The string to check.
 *
 * Check whether the given string follows the naming rules.
 * Returns true if @string follows the naming rules, false otherwise.
 */
bool tomoyo_correct_word(const char *string)
{
	return tomoyo_correct_word2(string, strlen(string));
}

/**
 * tomoyo_correct_path2 - Check whether the given pathname follows the naming rules.
 *
 * @filename: The pathname to check.
 * @len:      Length of @filename.
 *
 * Returns true if @filename follows the naming rules, false otherwise.
 */
static bool tomoyo_correct_path2(const char *filename, const size_t len)
{
	const char *cp1 = memchr(filename, '/', len);
	const char *cp2 = memchr(filename, '.', len);

	return cp1 && (!cp2 || (cp1 < cp2)) && tomoyo_correct_word2(filename, len);
}

/**
 * tomoyo_correct_path - Validate a pathname.
 *
 * @filename: The pathname to check.
 *
 * Check whether the given pathname follows the naming rules.
 * Returns true if @filename follows the naming rules, false otherwise.
 */
bool tomoyo_correct_path(const char *filename)
{
	return tomoyo_correct_path2(filename, strlen(filename));
}

/**
 * tomoyo_correct_domain - Check whether the given domainname follows the naming rules.
 *
 * @domainname: The domainname to check.
 *
 * Returns true if @domainname follows the naming rules, false otherwise.
 */
bool tomoyo_correct_domain(const unsigned char *domainname)
{
	if (!domainname || !tomoyo_domain_def(domainname))
		return false;
	domainname = strchr(domainname, ' ');
	if (!domainname++)
		return true;
	while (1) {
		const unsigned char *cp = strchr(domainname, ' ');

		if (!cp)
			break;
		if (!tomoyo_correct_path2(domainname, cp - domainname))
			return false;
		domainname = cp + 1;
	}
	return tomoyo_correct_path(domainname);
}

/**
 * tomoyo_domain_def - Check whether the given token can be a domainname.
 *
 * @buffer: The token to check.
 *
 * Returns true if @buffer possibly be a domainname, false otherwise.
 */
bool tomoyo_domain_def(const unsigned char *buffer)
{
	const unsigned char *cp;
	int len;

	if (*buffer != '<')
		return false;
	cp = strchr(buffer, ' ');
	if (!cp)
		len = strlen(buffer);
	else
		len = cp - buffer;
	if (buffer[len - 1] != '>' ||
	    !tomoyo_correct_word2(buffer + 1, len - 2))
		return false;
	return true;
}

/**
 * tomoyo_find_domain - Find a domain by the given name.
 *
 * @domainname: The domainname to find.
 *
 * Returns pointer to "struct tomoyo_domain_info" if found, NULL otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
struct tomoyo_domain_info *tomoyo_find_domain(const char *domainname)
{
	struct tomoyo_domain_info *domain;
	struct tomoyo_path_info name;

	name.name = domainname;
	tomoyo_fill_path_info(&name);
	list_for_each_entry_rcu(domain, &tomoyo_domain_list, list,
				srcu_read_lock_held(&tomoyo_ss)) {
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
	ptr->hash = full_name_hash(NULL, name, len);
}

/**
 * tomoyo_file_matches_pattern2 - Pattern matching without '/' character and "\-" pattern.
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
		int i;
		int j;

		if (*pattern != '\\') {
			if (*filename++ != *pattern++)
				return false;
			continue;
		}
		c = *filename;
		pattern++;
		switch (*pattern) {
		case '?':
			if (c == '/') {
				return false;
			} else if (c == '\\') {
				if (filename[1] == '\\')
					filename++;
				else if (tomoyo_byte_range(filename + 1))
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
			if (!tomoyo_alphabet_char(c))
				return false;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
			if (c == '\\' && tomoyo_byte_range(filename + 1)
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
				else if (tomoyo_byte_range(filename + i + 1))
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
				while (tomoyo_alphabet_char(filename[j]))
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
 * tomoyo_file_matches_pattern - Pattern matching without '/' character.
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
 * tomoyo_get_exe - Get tomoyo_realpath() of current process.
 *
 * Returns the tomoyo_realpath() of current process on success, NULL otherwise.
 *
 * This function uses kzalloc(), so the caller must call kfree()
 * if this function didn't return NULL.
 */
const char *tomoyo_get_exe(void)
{
	struct file *exe_file;
	const char *cp;
	struct mm_struct *mm = current->mm;

	if (!mm)
		return NULL;
	exe_file = get_mm_exe_file(mm);
	if (!exe_file)
		return NULL;

	cp = tomoyo_realpath_from_path(&exe_file->f_path);
	fput(exe_file);
	return cp;
}

/**
 * tomoyo_get_mode - Get MAC mode.
 *
 * @ns:      Pointer to "struct tomoyo_policy_namespace".
 * @profile: Profile number.
 * @index:   Index number of functionality.
 *
 * Returns mode.
 */
int tomoyo_get_mode(const struct tomoyo_policy_namespace *ns, const u8 profile,
		    const u8 index)
{
	u8 mode;
	struct tomoyo_profile *p;

	if (!tomoyo_policy_loaded)
		return TOMOYO_CONFIG_DISABLED;
	p = tomoyo_profile(ns, profile);
	mode = p->config[index];
	if (mode == TOMOYO_CONFIG_USE_DEFAULT)
		mode = p->config[tomoyo_index2category[index]
				 + TOMOYO_MAX_MAC_INDEX];
	if (mode == TOMOYO_CONFIG_USE_DEFAULT)
		mode = p->default_config;
	return mode & 3;
}

/**
 * tomoyo_init_request_info - Initialize "struct tomoyo_request_info" members.
 *
 * @r:      Pointer to "struct tomoyo_request_info" to initialize.
 * @domain: Pointer to "struct tomoyo_domain_info". NULL for tomoyo_domain().
 * @index:  Index number of functionality.
 *
 * Returns mode.
 */
int tomoyo_init_request_info(struct tomoyo_request_info *r,
			     struct tomoyo_domain_info *domain, const u8 index)
{
	u8 profile;

	memset(r, 0, sizeof(*r));
	if (!domain)
		domain = tomoyo_domain();
	r->domain = domain;
	profile = domain->profile;
	r->profile = profile;
	r->type = index;
	r->mode = tomoyo_get_mode(domain->ns, profile, index);
	return r->mode;
}

/**
 * tomoyo_domain_quota_is_ok - Check for domain's quota.
 *
 * @r: Pointer to "struct tomoyo_request_info".
 *
 * Returns true if the domain is not exceeded quota, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_domain_quota_is_ok(struct tomoyo_request_info *r)
{
	unsigned int count = 0;
	struct tomoyo_domain_info *domain = r->domain;
	struct tomoyo_acl_info *ptr;

	if (r->mode != TOMOYO_CONFIG_LEARNING)
		return false;
	if (!domain)
		return true;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list,
				srcu_read_lock_held(&tomoyo_ss)) {
		u16 perm;
		u8 i;

		if (ptr->is_deleted)
			continue;
		/*
		 * Reading perm bitmap might race with tomoyo_merge_*() because
		 * caller does not hold tomoyo_policy_lock mutex. But exceeding
		 * max_learning_entry parameter by a few entries does not harm.
		 */
		switch (ptr->type) {
		case TOMOYO_TYPE_PATH_ACL:
			data_race(perm = container_of(ptr, struct tomoyo_path_acl, head)->perm);
			break;
		case TOMOYO_TYPE_PATH2_ACL:
			data_race(perm = container_of(ptr, struct tomoyo_path2_acl, head)->perm);
			break;
		case TOMOYO_TYPE_PATH_NUMBER_ACL:
			data_race(perm = container_of(ptr, struct tomoyo_path_number_acl, head)
				  ->perm);
			break;
		case TOMOYO_TYPE_MKDEV_ACL:
			data_race(perm = container_of(ptr, struct tomoyo_mkdev_acl, head)->perm);
			break;
		case TOMOYO_TYPE_INET_ACL:
			data_race(perm = container_of(ptr, struct tomoyo_inet_acl, head)->perm);
			break;
		case TOMOYO_TYPE_UNIX_ACL:
			data_race(perm = container_of(ptr, struct tomoyo_unix_acl, head)->perm);
			break;
		case TOMOYO_TYPE_MANUAL_TASK_ACL:
			perm = 0;
			break;
		default:
			perm = 1;
		}
		for (i = 0; i < 16; i++)
			if (perm & (1 << i))
				count++;
	}
	if (count < tomoyo_profile(domain->ns, domain->profile)->
	    pref[TOMOYO_PREF_MAX_LEARNING_ENTRY])
		return true;
	if (!domain->flags[TOMOYO_DIF_QUOTA_WARNED]) {
		domain->flags[TOMOYO_DIF_QUOTA_WARNED] = true;
		/* r->granted = false; */
		tomoyo_write_log(r, "%s", tomoyo_dif[TOMOYO_DIF_QUOTA_WARNED]);
#ifndef CONFIG_SECURITY_TOMOYO_INSECURE_BUILTIN_SETTING
		pr_warn("WARNING: Domain '%s' has too many ACLs to hold. Stopped learning mode.\n",
			domain->domainname->name);
#endif
	}
	return false;
}
