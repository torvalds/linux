/*
 * Convert integer string representation to an integer.
 * If an integer doesn't fit into specified type, -E is returned.
 *
 * Integer starts with optional sign.
 * kstrtou*() functions do not accept sign "-".
 *
 * Radix 0 means autodetection: leading "0x" implies radix 16,
 * leading "0" implies radix 8, otherwise radix is 10.
 * Autodetection hints work after optional sign, but not before.
 *
 * If -E is returned, result is not touched.
 */
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/uaccess.h>

static int _kstrtoull(const char *s, unsigned int base, unsigned long long *res)
{
	unsigned long long acc;
	int ok;

	if (base == 0) {
		if (s[0] == '0') {
			if (_tolower(s[1]) == 'x' && isxdigit(s[2]))
				base = 16;
			else
				base = 8;
		} else
			base = 10;
	}
	if (base == 16 && s[0] == '0' && _tolower(s[1]) == 'x')
		s += 2;

	acc = 0;
	ok = 0;
	while (*s) {
		unsigned int val;

		if ('0' <= *s && *s <= '9')
			val = *s - '0';
		else if ('a' <= _tolower(*s) && _tolower(*s) <= 'f')
			val = _tolower(*s) - 'a' + 10;
		else if (*s == '\n' && *(s + 1) == '\0')
			break;
		else
			return -EINVAL;

		if (val >= base)
			return -EINVAL;
		if (acc > div_u64(ULLONG_MAX - val, base))
			return -ERANGE;
		acc = acc * base + val;
		ok = 1;

		s++;
	}
	if (!ok)
		return -EINVAL;
	*res = acc;
	return 0;
}

int kstrtoull(const char *s, unsigned int base, unsigned long long *res)
{
	if (s[0] == '+')
		s++;
	return _kstrtoull(s, base, res);
}
EXPORT_SYMBOL(kstrtoull);

int kstrtoll(const char *s, unsigned int base, long long *res)
{
	unsigned long long tmp;
	int rv;

	if (s[0] == '-') {
		rv = _kstrtoull(s + 1, base, &tmp);
		if (rv < 0)
			return rv;
		if ((long long)(-tmp) >= 0)
			return -ERANGE;
		*res = -tmp;
	} else {
		rv = kstrtoull(s, base, &tmp);
		if (rv < 0)
			return rv;
		if ((long long)tmp < 0)
			return -ERANGE;
		*res = tmp;
	}
	return 0;
}
EXPORT_SYMBOL(kstrtoll);

/* Internal, do not use. */
int _kstrtoul(const char *s, unsigned int base, unsigned long *res)
{
	unsigned long long tmp;
	int rv;

	rv = kstrtoull(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (unsigned long long)(unsigned long)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(_kstrtoul);

/* Internal, do not use. */
int _kstrtol(const char *s, unsigned int base, long *res)
{
	long long tmp;
	int rv;

	rv = kstrtoll(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (long long)(long)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(_kstrtol);

int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	unsigned long long tmp;
	int rv;

	rv = kstrtoull(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (unsigned long long)(unsigned int)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(kstrtouint);

int kstrtoint(const char *s, unsigned int base, int *res)
{
	long long tmp;
	int rv;

	rv = kstrtoll(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (long long)(int)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(kstrtoint);

int kstrtou16(const char *s, unsigned int base, u16 *res)
{
	unsigned long long tmp;
	int rv;

	rv = kstrtoull(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (unsigned long long)(u16)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(kstrtou16);

int kstrtos16(const char *s, unsigned int base, s16 *res)
{
	long long tmp;
	int rv;

	rv = kstrtoll(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (long long)(s16)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(kstrtos16);

int kstrtou8(const char *s, unsigned int base, u8 *res)
{
	unsigned long long tmp;
	int rv;

	rv = kstrtoull(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (unsigned long long)(u8)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(kstrtou8);

int kstrtos8(const char *s, unsigned int base, s8 *res)
{
	long long tmp;
	int rv;

	rv = kstrtoll(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (long long)(s8)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
EXPORT_SYMBOL(kstrtos8);

#define kstrto_from_user(f, g, type)					\
int f(const char __user *s, size_t count, unsigned int base, type *res)	\
{									\
	/* sign, base 2 representation, newline, terminator */		\
	char buf[1 + sizeof(type) * 8 + 1 + 1];				\
									\
	count = min(count, sizeof(buf) - 1);				\
	if (copy_from_user(buf, s, count))				\
		return -EFAULT;						\
	buf[count] = '\0';						\
	return g(buf, base, res);					\
}									\
EXPORT_SYMBOL(f)

kstrto_from_user(kstrtoull_from_user,	kstrtoull,	unsigned long long);
kstrto_from_user(kstrtoll_from_user,	kstrtoll,	long long);
kstrto_from_user(kstrtoul_from_user,	kstrtoul,	unsigned long);
kstrto_from_user(kstrtol_from_user,	kstrtol,	long);
kstrto_from_user(kstrtouint_from_user,	kstrtouint,	unsigned int);
kstrto_from_user(kstrtoint_from_user,	kstrtoint,	int);
kstrto_from_user(kstrtou16_from_user,	kstrtou16,	u16);
kstrto_from_user(kstrtos16_from_user,	kstrtos16,	s16);
kstrto_from_user(kstrtou8_from_user,	kstrtou8,	u8);
kstrto_from_user(kstrtos8_from_user,	kstrtos8,	s8);
