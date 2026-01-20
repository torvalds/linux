// SPDX-License-Identifier: GPL-2.0-only
/*
 * sysctl.c: General linux system control interface
 */

#include <linux/sysctl.h>
#include <linux/bitmap.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/highuid.h>
#include <linux/writeback.h>
#include <linux/initrd.h>
#include <linux/limits.h>
#include <linux/syscalls.h>
#include <linux/capability.h>

#include "../lib/kstrtox.h"

#include <linux/uaccess.h>
#include <asm/processor.h>

/* shared constants to be used in various sysctls */
const int sysctl_vals[] = { 0, 1, 2, 3, 4, 100, 200, 1000, 3000, INT_MAX, 65535, -1 };
EXPORT_SYMBOL(sysctl_vals);

const unsigned long sysctl_long_vals[] = { 0, 1, LONG_MAX };
EXPORT_SYMBOL_GPL(sysctl_long_vals);

#if defined(CONFIG_SYSCTL)

/* Constants used for minimum and maximum */
static const int ngroups_max = NGROUPS_MAX;
static const int cap_last_cap = CAP_LAST_CAP;

#ifdef CONFIG_PROC_SYSCTL

/**
 * enum sysctl_writes_mode - supported sysctl write modes
 *
 * @SYSCTL_WRITES_LEGACY: each write syscall must fully contain the sysctl value
 *	to be written, and multiple writes on the same sysctl file descriptor
 *	will rewrite the sysctl value, regardless of file position. No warning
 *	is issued when the initial position is not 0.
 * @SYSCTL_WRITES_WARN: same as above but warn when the initial file position is
 *	not 0.
 * @SYSCTL_WRITES_STRICT: writes to numeric sysctl entries must always be at
 *	file position 0 and the value must be fully contained in the buffer
 *	sent to the write syscall. If dealing with strings respect the file
 *	position, but restrict this to the max length of the buffer, anything
 *	passed the max length will be ignored. Multiple writes will append
 *	to the buffer.
 *
 * These write modes control how current file position affects the behavior of
 * updating internal kernel (SYSCTL_USER_TO_KERN) sysctl values through the proc
 * interface on each write.
 */
enum sysctl_writes_mode {
	SYSCTL_WRITES_LEGACY		= -1,
	SYSCTL_WRITES_WARN		= 0,
	SYSCTL_WRITES_STRICT		= 1,
};

static enum sysctl_writes_mode sysctl_writes_strict = SYSCTL_WRITES_STRICT;
#endif /* CONFIG_PROC_SYSCTL */
#endif /* CONFIG_SYSCTL */

/*
 * /proc/sys support
 */

#ifdef CONFIG_PROC_SYSCTL

static int _proc_do_string(char *data, int maxlen, int dir,
		char *buffer, size_t *lenp, loff_t *ppos)
{
	size_t len;
	char c, *p;

	if (!data || !maxlen || !*lenp) {
		*lenp = 0;
		return 0;
	}

	if (SYSCTL_USER_TO_KERN(dir)) {
		if (sysctl_writes_strict == SYSCTL_WRITES_STRICT) {
			/* Only continue writes not past the end of buffer. */
			len = strlen(data);
			if (len > maxlen - 1)
				len = maxlen - 1;

			if (*ppos > len)
				return 0;
			len = *ppos;
		} else {
			/* Start writing from beginning of buffer. */
			len = 0;
		}

		*ppos += *lenp;
		p = buffer;
		while ((p - buffer) < *lenp && len < maxlen - 1) {
			c = *(p++);
			if (c == 0 || c == '\n')
				break;
			data[len++] = c;
		}
		data[len] = 0;
	} else {
		len = strlen(data);
		if (len > maxlen)
			len = maxlen;

		if (*ppos > len) {
			*lenp = 0;
			return 0;
		}

		data += *ppos;
		len  -= *ppos;

		if (len > *lenp)
			len = *lenp;
		if (len)
			memcpy(buffer, data, len);
		if (len < *lenp) {
			buffer[len] = '\n';
			len++;
		}
		*lenp = len;
		*ppos += len;
	}
	return 0;
}

static void warn_sysctl_write(const struct ctl_table *table)
{
	pr_warn_once("%s wrote to %s when file position was not 0!\n"
		"This will not be supported in the future. To silence this\n"
		"warning, set kernel.sysctl_writes_strict = -1\n",
		current->comm, table->procname);
}

/**
 * proc_first_pos_non_zero_ignore - check if first position is allowed
 * @ppos: file position
 * @table: the sysctl table
 *
 * Returns true if the first position is non-zero and the sysctl_writes_strict
 * mode indicates this is not allowed for numeric input types. String proc
 * handlers can ignore the return value.
 */
static bool proc_first_pos_non_zero_ignore(loff_t *ppos,
					   const struct ctl_table *table)
{
	if (!*ppos)
		return false;

	switch (sysctl_writes_strict) {
	case SYSCTL_WRITES_STRICT:
		return true;
	case SYSCTL_WRITES_WARN:
		warn_sysctl_write(table);
		return false;
	default:
		return false;
	}
}

/**
 * proc_dostring - read a string sysctl
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes a string from/to the user buffer. If the kernel
 * buffer provided is not large enough to hold the string, the
 * string is truncated. The copied string is %NULL-terminated.
 * If the string is being read by the user process, it is copied
 * and a newline '\n' is added. It is truncated if the buffer is
 * not large enough.
 *
 * Returns 0 on success.
 */
int proc_dostring(const struct ctl_table *table, int dir,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	if (SYSCTL_USER_TO_KERN(dir))
		proc_first_pos_non_zero_ignore(ppos, table);

	return _proc_do_string(table->data, table->maxlen, dir, buffer, lenp,
			ppos);
}

static void proc_skip_spaces(char **buf, size_t *size)
{
	while (*size) {
		if (!isspace(**buf))
			break;
		(*size)--;
		(*buf)++;
	}
}

static void proc_skip_char(char **buf, size_t *size, const char v)
{
	while (*size) {
		if (**buf != v)
			break;
		(*size)--;
		(*buf)++;
	}
}

/**
 * strtoul_lenient - parse an ASCII formatted integer from a buffer and only
 *                   fail on overflow
 *
 * @cp: kernel buffer containing the string to parse
 * @endp: pointer to store the trailing characters
 * @base: the base to use
 * @res: where the parsed integer will be stored
 *
 * In case of success 0 is returned and @res will contain the parsed integer,
 * @endp will hold any trailing characters.
 * This function will fail the parse on overflow. If there wasn't an overflow
 * the function will defer the decision what characters count as invalid to the
 * caller.
 */
static int strtoul_lenient(const char *cp, char **endp, unsigned int base,
			   unsigned long *res)
{
	unsigned long long result;
	unsigned int rv;

	cp = _parse_integer_fixup_radix(cp, &base);
	rv = _parse_integer(cp, base, &result);
	if ((rv & KSTRTOX_OVERFLOW) || (result != (unsigned long)result))
		return -ERANGE;

	cp += rv;

	if (endp)
		*endp = (char *)cp;

	*res = (unsigned long)result;
	return 0;
}

#define TMPBUFLEN 22
/**
 * proc_get_long - reads an ASCII formatted integer from a user buffer
 *
 * @buf: a kernel buffer
 * @size: size of the kernel buffer
 * @val: this is where the number will be stored
 * @neg: set to %TRUE if number is negative
 * @perm_tr: a vector which contains the allowed trailers
 * @perm_tr_len: size of the perm_tr vector
 * @tr: pointer to store the trailer character
 *
 * In case of success %0 is returned and @buf and @size are updated with
 * the amount of bytes read. If @tr is non-NULL and a trailing
 * character exists (size is non-zero after returning from this
 * function), @tr is updated with the trailing character.
 */
static int proc_get_long(char **buf, size_t *size,
			  unsigned long *val, bool *neg,
			  const char *perm_tr, unsigned perm_tr_len, char *tr)
{
	char *p, tmp[TMPBUFLEN];
	ssize_t len = *size;

	if (len <= 0)
		return -EINVAL;

	if (len > TMPBUFLEN - 1)
		len = TMPBUFLEN - 1;

	memcpy(tmp, *buf, len);

	tmp[len] = 0;
	p = tmp;
	if (*p == '-' && *size > 1) {
		*neg = true;
		p++;
	} else
		*neg = false;
	if (!isdigit(*p))
		return -EINVAL;

	if (strtoul_lenient(p, &p, 0, val))
		return -EINVAL;

	len = p - tmp;

	/* We don't know if the next char is whitespace thus we may accept
	 * invalid integers (e.g. 1234...a) or two integers instead of one
	 * (e.g. 123...1). So lets not allow such large numbers. */
	if (len == TMPBUFLEN - 1)
		return -EINVAL;

	if (len < *size && perm_tr_len && !memchr(perm_tr, *p, perm_tr_len))
		return -EINVAL;

	if (tr && (len < *size))
		*tr = *p;

	*buf += len;
	*size -= len;

	return 0;
}

/**
 * proc_put_long - converts an integer to a decimal ASCII formatted string
 *
 * @buf: the user buffer
 * @size: the size of the user buffer
 * @val: the integer to be converted
 * @neg: sign of the number, %TRUE for negative
 *
 * In case of success @buf and @size are updated with the amount of bytes
 * written.
 */
static void proc_put_long(void **buf, size_t *size, unsigned long val, bool neg)
{
	int len;
	char tmp[TMPBUFLEN], *p = tmp;

	sprintf(p, "%s%lu", neg ? "-" : "", val);
	len = strlen(tmp);
	if (len > *size)
		len = *size;
	memcpy(*buf, tmp, len);
	*size -= len;
	*buf += len;
}
#undef TMPBUFLEN

static void proc_put_char(void **buf, size_t *size, char c)
{
	if (*size) {
		char **buffer = (char **)buf;
		**buffer = c;

		(*size)--;
		(*buffer)++;
		*buf = *buffer;
	}
}

static SYSCTL_USER_TO_KERN_INT_CONV(, SYSCTL_CONV_IDENTITY)
static SYSCTL_KERN_TO_USER_INT_CONV(, SYSCTL_CONV_IDENTITY)

static SYSCTL_INT_CONV_CUSTOM(, sysctl_user_to_kern_int_conv,
			      sysctl_kern_to_user_int_conv, false)
static SYSCTL_INT_CONV_CUSTOM(_minmax, sysctl_user_to_kern_int_conv,
			      sysctl_kern_to_user_int_conv, true)


static SYSCTL_USER_TO_KERN_UINT_CONV(, SYSCTL_CONV_IDENTITY)

int sysctl_kern_to_user_uint_conv(unsigned long *u_ptr,
				  const unsigned int *k_ptr)
{
	unsigned int val = READ_ONCE(*k_ptr);
	*u_ptr = (unsigned long)val;
	return 0;
}

static SYSCTL_UINT_CONV_CUSTOM(, sysctl_user_to_kern_uint_conv,
			       sysctl_kern_to_user_uint_conv, false)
static SYSCTL_UINT_CONV_CUSTOM(_minmax, sysctl_user_to_kern_uint_conv,
			       sysctl_kern_to_user_uint_conv, true)

static const char proc_wspace_sep[] = { ' ', '\t', '\n' };

static int do_proc_dointvec(const struct ctl_table *table, int dir,
		  void *buffer, size_t *lenp, loff_t *ppos,
		  int (*conv)(bool *negp, unsigned long *u_ptr, int *k_ptr,
			      int dir, const struct ctl_table *table))
{
	int *i, vleft, first = 1, err = 0;
	size_t left;
	char *p;

	if (!table->data || !table->maxlen || !*lenp ||
	    (*ppos && SYSCTL_KERN_TO_USER(dir))) {
		*lenp = 0;
		return 0;
	}

	i = (int *) table->data;
	vleft = table->maxlen / sizeof(*i);
	left = *lenp;

	if (!conv)
		conv = do_proc_int_conv;

	if (SYSCTL_USER_TO_KERN(dir)) {
		if (proc_first_pos_non_zero_ignore(ppos, table))
			goto out;

		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;
		p = buffer;
	}

	for (; left && vleft--; i++, first=0) {
		unsigned long lval;
		bool neg;

		if (SYSCTL_USER_TO_KERN(dir)) {
			proc_skip_spaces(&p, &left);

			if (!left)
				break;
			err = proc_get_long(&p, &left, &lval, &neg,
					     proc_wspace_sep,
					     sizeof(proc_wspace_sep), NULL);
			if (err)
				break;
			if (conv(&neg, &lval, i, 1, table)) {
				err = -EINVAL;
				break;
			}
		} else {
			if (conv(&neg, &lval, i, 0, table)) {
				err = -EINVAL;
				break;
			}
			if (!first)
				proc_put_char(&buffer, &left, '\t');
			proc_put_long(&buffer, &left, lval, neg);
		}
	}

	if (SYSCTL_KERN_TO_USER(dir) && !first && left && !err)
		proc_put_char(&buffer, &left, '\n');
	if (SYSCTL_USER_TO_KERN(dir) && !err && left)
		proc_skip_spaces(&p, &left);
	if (SYSCTL_USER_TO_KERN(dir) && first)
		return err ? : -EINVAL;
	*lenp -= left;
out:
	*ppos += *lenp;
	return err;
}

static int do_proc_douintvec_w(const struct ctl_table *table, void *buffer,
			       size_t *lenp, loff_t *ppos,
			       int (*conv)(unsigned long *u_ptr,
					   unsigned int *k_ptr, int dir,
					   const struct ctl_table *table))
{
	unsigned long lval;
	int err = 0;
	size_t left;
	bool neg;
	char *p = buffer;

	left = *lenp;

	if (proc_first_pos_non_zero_ignore(ppos, table))
		goto bail_early;

	if (left > PAGE_SIZE - 1)
		left = PAGE_SIZE - 1;

	proc_skip_spaces(&p, &left);
	if (!left) {
		err = -EINVAL;
		goto out_free;
	}

	err = proc_get_long(&p, &left, &lval, &neg,
			     proc_wspace_sep,
			     sizeof(proc_wspace_sep), NULL);
	if (err || neg) {
		err = -EINVAL;
		goto out_free;
	}

	if (conv(&lval, (unsigned int *) table->data, 1, table)) {
		err = -EINVAL;
		goto out_free;
	}

	if (!err && left)
		proc_skip_spaces(&p, &left);

out_free:
	if (err)
		return -EINVAL;

	return 0;

bail_early:
	*ppos += *lenp;
	return err;
}

static int do_proc_douintvec_r(const struct ctl_table *table, void *buffer,
			       size_t *lenp, loff_t *ppos,
			       int (*conv)(unsigned long *u_ptr,
					   unsigned int *k_ptr, int dir,
					   const struct ctl_table *table))
{
	unsigned long lval;
	int err = 0;
	size_t left;

	left = *lenp;

	if (conv(&lval, (unsigned int *) table->data, 0, table)) {
		err = -EINVAL;
		goto out;
	}

	proc_put_long(&buffer, &left, lval, false);
	if (!left)
		goto out;

	proc_put_char(&buffer, &left, '\n');

out:
	*lenp -= left;
	*ppos += *lenp;

	return err;
}

static int do_proc_douintvec(const struct ctl_table *table, int dir,
			     void *buffer, size_t *lenp, loff_t *ppos,
			      int (*conv)(unsigned long *u_ptr,
					  unsigned int *k_ptr, int dir,
					  const struct ctl_table *table))
{
	unsigned int vleft;

	if (!table->data || !table->maxlen || !*lenp ||
	    (*ppos && SYSCTL_KERN_TO_USER(dir))) {
		*lenp = 0;
		return 0;
	}

	vleft = table->maxlen / sizeof(unsigned int);

	/*
	 * Arrays are not supported, keep this simple. *Do not* add
	 * support for them.
	 */
	if (vleft != 1) {
		*lenp = 0;
		return -EINVAL;
	}

	if (!conv)
		conv = do_proc_uint_conv;

	if (SYSCTL_USER_TO_KERN(dir))
		return do_proc_douintvec_w(table, buffer, lenp, ppos, conv);
	return do_proc_douintvec_r(table, buffer, lenp, ppos, conv);
}

int proc_douintvec_conv(const struct ctl_table *table, int dir, void *buffer,
			size_t *lenp, loff_t *ppos,
			int (*conv)(unsigned long *u_ptr, unsigned int *k_ptr,
				    int dir, const struct ctl_table *table))
{
	return do_proc_douintvec(table, dir, buffer, lenp, ppos, conv);
}


/**
 * proc_dobool - read/write a bool
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes one integer value from/to the user buffer,
 * treated as an ASCII string.
 *
 * table->data must point to a bool variable and table->maxlen must
 * be sizeof(bool).
 *
 * Returns 0 on success.
 */
int proc_dobool(const struct ctl_table *table, int dir, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	struct ctl_table tmp;
	bool *data = table->data;
	int res, val;

	/* Do not support arrays yet. */
	if (table->maxlen != sizeof(bool))
		return -EINVAL;

	tmp = *table;
	tmp.maxlen = sizeof(val);
	tmp.data = &val;

	val = READ_ONCE(*data);
	res = proc_dointvec(&tmp, dir, buffer, lenp, ppos);
	if (res)
		return res;
	if (SYSCTL_USER_TO_KERN(dir))
		WRITE_ONCE(*data, val);
	return 0;
}

/**
 * proc_dointvec - read a vector of integers
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * Returns 0 on success.
 */
int proc_dointvec(const struct ctl_table *table, int dir, void *buffer,
		  size_t *lenp, loff_t *ppos)
{
	return do_proc_dointvec(table, dir, buffer, lenp, ppos, NULL);
}

/**
 * proc_douintvec - read a vector of unsigned integers
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) unsigned integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * Returns 0 on success.
 */
int proc_douintvec(const struct ctl_table *table, int dir, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	return do_proc_douintvec(table, dir, buffer, lenp, ppos,
				 do_proc_uint_conv);
}

/**
 * proc_dointvec_minmax - read a vector of integers with min/max values
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success or -EINVAL when the range check fails and
 * SYSCTL_USER_TO_KERN(dir) == true
 */
int proc_dointvec_minmax(const struct ctl_table *table, int dir,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_dointvec(table, dir, buffer, lenp, ppos,
				do_proc_int_conv_minmax);
}

/**
 * proc_douintvec_minmax - read a vector of unsigned ints with min/max values
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) unsigned integer
 * values from/to the user buffer, treated as an ASCII string. Negative
 * strings are not allowed.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max). There is a final sanity
 * check for UINT_MAX to avoid having to support wrap around uses from
 * userspace.
 *
 * Returns 0 on success or -ERANGE when range check failes and
 * SYSCTL_USER_TO_KERN(dir) == true
 */
int proc_douintvec_minmax(const struct ctl_table *table, int dir,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_douintvec(table, dir, buffer, lenp, ppos,
				 do_proc_uint_conv_minmax);
}

/**
 * proc_dou8vec_minmax - read a vector of unsigned chars with min/max values
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(u8) unsigned chars
 * values from/to the user buffer, treated as an ASCII string. Negative
 * strings are not allowed.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success or an error on SYSCTL_USER_TO_KERN(dir) == true
 * and the range check fails.
 */
int proc_dou8vec_minmax(const struct ctl_table *table, int dir,
			void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table tmp;
	unsigned int min = 0, max = 255U, val;
	u8 *data = table->data;
	int res;

	/* Do not support arrays yet. */
	if (table->maxlen != sizeof(u8))
		return -EINVAL;

	tmp = *table;

	tmp.maxlen = sizeof(val);
	tmp.data = &val;
	if (!tmp.extra1)
		tmp.extra1 = (unsigned int *) &min;
	if (!tmp.extra2)
		tmp.extra2 = (unsigned int *) &max;

	val = READ_ONCE(*data);
	res = do_proc_douintvec(&tmp, dir, buffer, lenp, ppos,
				do_proc_uint_conv_minmax);
	if (res)
		return res;
	if (SYSCTL_USER_TO_KERN(dir))
		WRITE_ONCE(*data, val);
	return 0;
}
EXPORT_SYMBOL_GPL(proc_dou8vec_minmax);

static int do_proc_doulongvec_minmax(const struct ctl_table *table, int dir,
				     void *buffer, size_t *lenp, loff_t *ppos,
				     unsigned long convmul,
				     unsigned long convdiv)
{
	unsigned long *i, *min, *max;
	int vleft, first = 1, err = 0;
	size_t left;
	char *p;

	if (!table->data || !table->maxlen || !*lenp ||
	    (*ppos && SYSCTL_KERN_TO_USER(dir))) {
		*lenp = 0;
		return 0;
	}

	i = table->data;
	min = table->extra1;
	max = table->extra2;
	vleft = table->maxlen / sizeof(unsigned long);
	left = *lenp;

	if (SYSCTL_USER_TO_KERN(dir)) {
		if (proc_first_pos_non_zero_ignore(ppos, table))
			goto out;

		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;
		p = buffer;
	}

	for (; left && vleft--; i++, first = 0) {
		unsigned long val;

		if (SYSCTL_USER_TO_KERN(dir)) {
			bool neg;

			proc_skip_spaces(&p, &left);
			if (!left)
				break;

			err = proc_get_long(&p, &left, &val, &neg,
					     proc_wspace_sep,
					     sizeof(proc_wspace_sep), NULL);
			if (err || neg) {
				err = -EINVAL;
				break;
			}

			val = convmul * val / convdiv;
			if ((min && val < *min) || (max && val > *max)) {
				err = -EINVAL;
				break;
			}
			WRITE_ONCE(*i, val);
		} else {
			val = convdiv * READ_ONCE(*i) / convmul;
			if (!first)
				proc_put_char(&buffer, &left, '\t');
			proc_put_long(&buffer, &left, val, false);
		}
	}

	if (SYSCTL_KERN_TO_USER(dir) && !first && left && !err)
		proc_put_char(&buffer, &left, '\n');
	if (SYSCTL_USER_TO_KERN(dir) && !err)
		proc_skip_spaces(&p, &left);
	if (SYSCTL_USER_TO_KERN(dir) && first)
		return err ? : -EINVAL;
	*lenp -= left;
out:
	*ppos += *lenp;
	return err;
}

int proc_doulongvec_minmax_conv(const struct ctl_table *table, int dir,
				void *buffer, size_t *lenp, loff_t *ppos,
				unsigned long convmul, unsigned long convdiv)
{
	return do_proc_doulongvec_minmax(table, dir, buffer, lenp, ppos,
					 convmul, convdiv);
}

/**
 * proc_doulongvec_minmax - read a vector of long integers with min/max values
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned long) unsigned long
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_doulongvec_minmax(const struct ctl_table *table, int dir,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_doulongvec_minmax_conv(table, dir, buffer, lenp, ppos, 1l, 1l);
}

int proc_dointvec_conv(const struct ctl_table *table, int dir, void *buffer,
		       size_t *lenp, loff_t *ppos,
		       int (*conv)(bool *negp, unsigned long *u_ptr, int *k_ptr,
				   int dir, const struct ctl_table *table))
{
	return do_proc_dointvec(table, dir, buffer, lenp, ppos, conv);
}

/**
 * proc_do_large_bitmap - read/write from/to a large bitmap
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * The bitmap is stored at table->data and the bitmap length (in bits)
 * in table->maxlen.
 *
 * We use a range comma separated format (e.g. 1,3-4,10-10) so that
 * large bitmaps may be represented in a compact manner. Writing into
 * the file will clear the bitmap then update it with the given input.
 *
 * Returns 0 on success.
 */
int proc_do_large_bitmap(const struct ctl_table *table, int dir,
			 void *buffer, size_t *lenp, loff_t *ppos)
{
	int err = 0;
	size_t left = *lenp;
	unsigned long bitmap_len = table->maxlen;
	unsigned long *bitmap = *(unsigned long **) table->data;
	unsigned long *tmp_bitmap = NULL;
	char tr_a[] = { '-', ',', '\n' }, tr_b[] = { ',', '\n', 0 }, c;

	if (!bitmap || !bitmap_len || !left || (*ppos && SYSCTL_KERN_TO_USER(dir))) {
		*lenp = 0;
		return 0;
	}

	if (SYSCTL_USER_TO_KERN(dir)) {
		char *p = buffer;
		size_t skipped = 0;

		if (left > PAGE_SIZE - 1) {
			left = PAGE_SIZE - 1;
			/* How much of the buffer we'll skip this pass */
			skipped = *lenp - left;
		}

		tmp_bitmap = bitmap_zalloc(bitmap_len, GFP_KERNEL);
		if (!tmp_bitmap)
			return -ENOMEM;
		proc_skip_char(&p, &left, '\n');
		while (!err && left) {
			unsigned long val_a, val_b;
			bool neg;
			size_t saved_left;

			/* In case we stop parsing mid-number, we can reset */
			saved_left = left;
			err = proc_get_long(&p, &left, &val_a, &neg, tr_a,
					     sizeof(tr_a), &c);
			/*
			 * If we consumed the entirety of a truncated buffer or
			 * only one char is left (may be a "-"), then stop here,
			 * reset, & come back for more.
			 */
			if ((left <= 1) && skipped) {
				left = saved_left;
				break;
			}

			if (err)
				break;
			if (val_a >= bitmap_len || neg) {
				err = -EINVAL;
				break;
			}

			val_b = val_a;
			if (left) {
				p++;
				left--;
			}

			if (c == '-') {
				err = proc_get_long(&p, &left, &val_b,
						     &neg, tr_b, sizeof(tr_b),
						     &c);
				/*
				 * If we consumed all of a truncated buffer or
				 * then stop here, reset, & come back for more.
				 */
				if (!left && skipped) {
					left = saved_left;
					break;
				}

				if (err)
					break;
				if (val_b >= bitmap_len || neg ||
				    val_a > val_b) {
					err = -EINVAL;
					break;
				}
				if (left) {
					p++;
					left--;
				}
			}

			bitmap_set(tmp_bitmap, val_a, val_b - val_a + 1);
			proc_skip_char(&p, &left, '\n');
		}
		left += skipped;
	} else {
		unsigned long bit_a, bit_b = 0;
		bool first = 1;

		while (left) {
			bit_a = find_next_bit(bitmap, bitmap_len, bit_b);
			if (bit_a >= bitmap_len)
				break;
			bit_b = find_next_zero_bit(bitmap, bitmap_len,
						   bit_a + 1) - 1;

			if (!first)
				proc_put_char(&buffer, &left, ',');
			proc_put_long(&buffer, &left, bit_a, false);
			if (bit_a != bit_b) {
				proc_put_char(&buffer, &left, '-');
				proc_put_long(&buffer, &left, bit_b, false);
			}

			first = 0; bit_b++;
		}
		proc_put_char(&buffer, &left, '\n');
	}

	if (!err) {
		if (SYSCTL_USER_TO_KERN(dir)) {
			if (*ppos)
				bitmap_or(bitmap, bitmap, tmp_bitmap, bitmap_len);
			else
				bitmap_copy(bitmap, tmp_bitmap, bitmap_len);
		}
		*lenp -= left;
		*ppos += *lenp;
	}

	bitmap_free(tmp_bitmap);
	return err;
}

#else /* CONFIG_PROC_SYSCTL */

int proc_dostring(const struct ctl_table *table, int dir,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dobool(const struct ctl_table *table, int dir,
		void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec(const struct ctl_table *table, int dir,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_douintvec(const struct ctl_table *table, int dir,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(const struct ctl_table *table, int dir,
		    void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_douintvec_minmax(const struct ctl_table *table, int dir,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dou8vec_minmax(const struct ctl_table *table, int dir,
			void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_minmax(const struct ctl_table *table, int dir,
		    void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_minmax_conv(const struct ctl_table *table, int dir,
				void *buffer, size_t *lenp, loff_t *ppos,
				unsigned long convmul, unsigned long convdiv)
{
	return -ENOSYS;
}

int proc_dointvec_conv(const struct ctl_table *table, int dir, void *buffer,
		       size_t *lenp, loff_t *ppos,
		       int (*conv)(bool *negp, unsigned long *u_ptr, int *k_ptr,
				   int dir, const struct ctl_table *table))
{
	return -ENOSYS;
}

int proc_do_large_bitmap(const struct ctl_table *table, int dir,
			 void *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

#endif /* CONFIG_PROC_SYSCTL */

#if defined(CONFIG_SYSCTL)
int proc_do_static_key(const struct ctl_table *table, int dir,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	struct static_key *key = (struct static_key *)table->data;
	static DEFINE_MUTEX(static_key_mutex);
	int val, ret;
	struct ctl_table tmp = {
		.data   = &val,
		.maxlen = sizeof(val),
		.mode   = table->mode,
		.extra1 = SYSCTL_ZERO,
		.extra2 = SYSCTL_ONE,
	};

	if (SYSCTL_USER_TO_KERN(dir) && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&static_key_mutex);
	val = static_key_enabled(key);
	ret = proc_dointvec_minmax(&tmp, dir, buffer, lenp, ppos);
	if (SYSCTL_USER_TO_KERN(dir) && !ret) {
		if (val)
			static_key_enable(key);
		else
			static_key_disable(key);
	}
	mutex_unlock(&static_key_mutex);
	return ret;
}

static const struct ctl_table sysctl_subsys_table[] = {
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "sysctl_writes_strict",
		.data		= &sysctl_writes_strict,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_NEG_ONE,
		.extra2		= SYSCTL_ONE,
	},
#endif
	{
		.procname	= "ngroups_max",
		.data		= (void *)&ngroups_max,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "cap_last_cap",
		.data		= (void *)&cap_last_cap,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_SYSCTL_ARCH_UNALIGN_ALLOW
	{
		.procname	= "unaligned-trap",
		.data		= &unaligned_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_SYSCTL_ARCH_UNALIGN_NO_WARN
	{
		.procname	= "ignore-unaligned-usertrap",
		.data		= &no_unaligned_warning,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
};

int __init sysctl_init_bases(void)
{
	register_sysctl_init("kernel", sysctl_subsys_table);

	return 0;
}
#endif /* CONFIG_SYSCTL */
/*
 * No sense putting this after each symbol definition, twice,
 * exception granted :-)
 */
EXPORT_SYMBOL(proc_dobool);
EXPORT_SYMBOL(proc_dointvec);
EXPORT_SYMBOL(proc_douintvec);
EXPORT_SYMBOL(proc_dointvec_minmax);
EXPORT_SYMBOL_GPL(proc_douintvec_minmax);
EXPORT_SYMBOL(proc_dostring);
EXPORT_SYMBOL(proc_doulongvec_minmax);
EXPORT_SYMBOL(proc_do_large_bitmap);
