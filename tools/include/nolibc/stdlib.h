/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * stdlib function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_STDLIB_H
#define _NOLIBC_STDLIB_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"
#include "string.h"
#include <linux/auxvec.h>

struct nolibc_heap {
	size_t	len;
	char	user_p[] __attribute__((__aligned__));
};

/* Buffer used to store int-to-ASCII conversions. Will only be implemented if
 * any of the related functions is implemented. The area is large enough to
 * store "18446744073709551615" or "-9223372036854775808" and the final zero.
 */
static __attribute__((unused)) char itoa_buffer[21];

/*
 * As much as possible, please keep functions alphabetically sorted.
 */

static __inline__
int abs(int j)
{
	return j >= 0 ? j : -j;
}

static __inline__
long labs(long j)
{
	return j >= 0 ? j : -j;
}

static __inline__
long long llabs(long long j)
{
	return j >= 0 ? j : -j;
}

/* must be exported, as it's used by libgcc for various divide functions */
void abort(void);
__attribute__((weak,unused,noreturn,section(".text.nolibc_abort")))
void abort(void)
{
	sys_kill(sys_getpid(), SIGABRT);
	for (;;);
}

static __attribute__((unused))
long atol(const char *s)
{
	unsigned long ret = 0;
	unsigned long d;
	int neg = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}

	while (1) {
		d = (*s++) - '0';
		if (d > 9)
			break;
		ret *= 10;
		ret += d;
	}

	return neg ? -ret : ret;
}

static __attribute__((unused))
int atoi(const char *s)
{
	return atol(s);
}

static __attribute__((unused))
void free(void *ptr)
{
	struct nolibc_heap *heap;

	if (!ptr)
		return;

	heap = container_of(ptr, struct nolibc_heap, user_p);
	munmap(heap, heap->len);
}

#ifndef NOLIBC_NO_RUNTIME
/* getenv() tries to find the environment variable named <name> in the
 * environment array pointed to by global variable "environ" which must be
 * declared as a char **, and must be terminated by a NULL (it is recommended
 * to set this variable to the "envp" argument of main()). If the requested
 * environment variable exists its value is returned otherwise NULL is
 * returned.
 */
static __attribute__((unused))
char *getenv(const char *name)
{
	int idx, i;

	if (environ) {
		for (idx = 0; environ[idx]; idx++) {
			for (i = 0; name[i] && name[i] == environ[idx][i];)
				i++;
			if (!name[i] && environ[idx][i] == '=')
				return &environ[idx][i+1];
		}
	}
	return NULL;
}
#endif /* NOLIBC_NO_RUNTIME */

static __attribute__((unused))
void *malloc(size_t len)
{
	struct nolibc_heap *heap;

	/* Always allocate memory with size multiple of 4096. */
	len  = sizeof(*heap) + len;
	len  = (len + 4095UL) & -4096UL;
	heap = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE,
		    -1, 0);
	if (__builtin_expect(heap == MAP_FAILED, 0))
		return NULL;

	heap->len = len;
	return heap->user_p;
}

static __attribute__((unused))
void *calloc(size_t size, size_t nmemb)
{
	size_t x = size * nmemb;

	if (__builtin_expect(size && ((x / size) != nmemb), 0)) {
		SET_ERRNO(ENOMEM);
		return NULL;
	}

	/*
	 * No need to zero the heap, the MAP_ANONYMOUS in malloc()
	 * already does it.
	 */
	return malloc(x);
}

static __attribute__((unused))
void *realloc(void *old_ptr, size_t new_size)
{
	struct nolibc_heap *heap;
	size_t user_p_len;
	void *ret;

	if (!old_ptr)
		return malloc(new_size);

	heap = container_of(old_ptr, struct nolibc_heap, user_p);
	user_p_len = heap->len - sizeof(*heap);
	/*
	 * Don't realloc() if @user_p_len >= @new_size, this block of
	 * memory is still enough to handle the @new_size. Just return
	 * the same pointer.
	 */
	if (user_p_len >= new_size)
		return old_ptr;

	ret = malloc(new_size);
	if (__builtin_expect(!ret, 0))
		return NULL;

	memcpy(ret, heap->user_p, user_p_len);
	munmap(heap, heap->len);
	return ret;
}

/* Converts the unsigned 64bit integer <in> to base <base> ascii into
 * buffer <buffer>, which must be long enough to store the number and the
 * trailing zero. The buffer is filled from the first byte, and the number
 * of characters emitted (not counting the trailing zero) is returned.
 * The function uses 'multiply by reciprocal' for the divisions and
 * requires the caller pass the correct reciprocal.
 *
 * Note that unlike __div64_const32() in asm-generic/div64.h there isn't
 * an extra shift done (by ___p), the reciprocal has to be lower resulting
 * in a slightly low quotient.
 * Keep things simple by correcting for the error.
 * This also saves calculating the 'low * low' product (e2 below) which is
 * very unlikely to be significant.
 *
 * Some maths:
 *	recip = p2 / base - e1;		// With e1 < base.
 *	q = (recip * in - e2) / p2;	// With e2 < p2.
 *        = base / in - (e1 * in + e2) / p2;
 *        > base / in - (e1 * p2 + p2) / p2;
 *        = base / in - ((e1 + 1) * p2) / p2;
 *        > base / in - base;
 * So the maximum error is less than 'base'.
 * Hence the largest possible digit is '2 * base - 1'.
 * For base 10 e1 is 6 and you can get digits of 15 (eg from 2**64-1).
 * Error e1 is largest for a base that is a factor of 2**64+1, the smallest is 274177
 * and converting 2**42-1 in base 274177 does generate a digit of 274177+274175.
 * This all means only a single correction is needed rather than a loop.
 *
 * __int128 isn't used for mips because gcc prior to 10.0 will call
 * __multi3 for MIPS64r6. The same also happens for SPARC and clang.
 */
#define _NOLIBC_U64TOA_RECIP(base) ((base) & 1 ? ~0ull / (base) : (1ull << 63) / ((base) / 2))
static __attribute__((unused, noinline))
int _nolibc_u64toa_base(uint64_t in, char *buffer, unsigned int base, uint64_t recip)
{
	unsigned int digits = 0;
	unsigned int dig;
	uint64_t q;
	char *p;

	/* Generate least significant digit first */
	do {
#if defined(__SIZEOF_INT128__) && !defined(__mips__) && !defined(__sparc__)
		q = ((unsigned __int128)in * recip) >> 64;
#else
		uint64_t p = (uint32_t)in * (recip >> 32);
		q = (in >> 32) * (recip >> 32) + (p >> 32);
		p = (uint32_t)p + (in >> 32) * (uint32_t)recip;
		q += p >> 32;
#endif
		dig = in - q * base;
		/* Correct for any rounding errors */
		if (dig >= base) {
			dig -= base;
			q++;
		}
		if (dig > 9)
			dig += 'a' - '0' - 10;
		buffer[digits++] = '0' + dig;
	} while ((in = q));

	buffer[digits] = 0;

	/* Order reverse to result */
	for (p = buffer + digits - 1; p > buffer; buffer++, p--) {
		dig = *buffer;
		*buffer = *p;
		*p = dig;
	}

	return digits;
}

/* Converts the unsigned long integer <in> to its hex representation into
 * buffer <buffer>, which must be long enough to store the number and the
 * trailing zero (17 bytes for "ffffffffffffffff" or 9 for "ffffffff"). The
 * buffer is filled from the first byte, and the number of characters emitted
 * (not counting the trailing zero) is returned.
 */
static __inline__ __attribute__((unused))
int utoh_r(unsigned long in, char *buffer)
{
	return _nolibc_u64toa_base(in, buffer, 16, _NOLIBC_U64TOA_RECIP(16));
}

/* converts unsigned long <in> to an hex string using the static itoa_buffer
 * and returns the pointer to that string.
 */
static __inline__ __attribute__((unused))
char *utoh(unsigned long in)
{
	utoh_r(in, itoa_buffer);
	return itoa_buffer;
}

/* Converts the unsigned long integer <in> to its string representation into
 * buffer <buffer>, which must be long enough to store the number and the
 * trailing zero (21 bytes for 18446744073709551615 in 64-bit, 11 for
 * 4294967295 in 32-bit). The buffer is filled from the first byte, and the
 * number of characters emitted (not counting the trailing zero) is returned.
 */
static __inline__ __attribute__((unused))
int utoa_r(unsigned long in, char *buffer)
{
	return _nolibc_u64toa_base(in, buffer, 10, _NOLIBC_U64TOA_RECIP(10));
}

/* Converts the signed long integer <in> to its string representation into
 * buffer <buffer>, which must be long enough to store the number and the
 * trailing zero (21 bytes for -9223372036854775808 in 64-bit, 12 for
 * -2147483648 in 32-bit). The buffer is filled from the first byte, and the
 * number of characters emitted (not counting the trailing zero) is returned.
 */
static __attribute__((unused))
int itoa_r(long in, char *buffer)
{
	char *ptr = buffer;
	int len = 0;

	if (in < 0) {
		in = -(unsigned long)in;
		*(ptr++) = '-';
		len++;
	}
	len += utoa_r(in, ptr);
	return len;
}

/* for historical compatibility, same as above but returns the pointer to the
 * buffer.
 */
static __inline__ __attribute__((unused))
char *ltoa_r(long in, char *buffer)
{
	itoa_r(in, buffer);
	return buffer;
}

/* converts long integer <in> to a string using the static itoa_buffer and
 * returns the pointer to that string.
 */
static __inline__ __attribute__((unused))
char *itoa(long in)
{
	itoa_r(in, itoa_buffer);
	return itoa_buffer;
}

/* converts long integer <in> to a string using the static itoa_buffer and
 * returns the pointer to that string. Same as above, for compatibility.
 */
static __inline__ __attribute__((unused))
char *ltoa(long in)
{
	itoa_r(in, itoa_buffer);
	return itoa_buffer;
}

/* converts unsigned long integer <in> to a string using the static itoa_buffer
 * and returns the pointer to that string.
 */
static __inline__ __attribute__((unused))
char *utoa(unsigned long in)
{
	utoa_r(in, itoa_buffer);
	return itoa_buffer;
}

/* Converts the unsigned 64-bit integer <in> to its hex representation into
 * buffer <buffer>, which must be long enough to store the number and the
 * trailing zero (17 bytes for "ffffffffffffffff"). The buffer is filled from
 * the first byte, and the number of characters emitted (not counting the
 * trailing zero) is returned.
 */
static __inline__ __attribute__((unused))
int u64toh_r(uint64_t in, char *buffer)
{
	return _nolibc_u64toa_base(in, buffer, 16, _NOLIBC_U64TOA_RECIP(16));
}

/* converts uint64_t <in> to an hex string using the static itoa_buffer and
 * returns the pointer to that string.
 */
static __inline__ __attribute__((unused))
char *u64toh(uint64_t in)
{
	u64toh_r(in, itoa_buffer);
	return itoa_buffer;
}

/* Converts the unsigned 64-bit integer <in> to its string representation into
 * buffer <buffer>, which must be long enough to store the number and the
 * trailing zero (21 bytes for 18446744073709551615). The buffer is filled from
 * the first byte, and the number of characters emitted (not counting the
 * trailing zero) is returned.
 */
static __inline__ __attribute__((unused))
int u64toa_r(uint64_t in, char *buffer)
{
	return _nolibc_u64toa_base(in, buffer, 10, _NOLIBC_U64TOA_RECIP(10));
}

/* Converts the signed 64-bit integer <in> to its string representation into
 * buffer <buffer>, which must be long enough to store the number and the
 * trailing zero (21 bytes for -9223372036854775808). The buffer is filled from
 * the first byte, and the number of characters emitted (not counting the
 * trailing zero) is returned.
 */
static __attribute__((unused))
int i64toa_r(int64_t in, char *buffer)
{
	char *ptr = buffer;
	int len = 0;

	if (in < 0) {
		in = -(uint64_t)in;
		*(ptr++) = '-';
		len++;
	}
	len += u64toa_r(in, ptr);
	return len;
}

/* converts int64_t <in> to a string using the static itoa_buffer and returns
 * the pointer to that string.
 */
static __inline__ __attribute__((unused))
char *i64toa(int64_t in)
{
	i64toa_r(in, itoa_buffer);
	return itoa_buffer;
}

/* converts uint64_t <in> to a string using the static itoa_buffer and returns
 * the pointer to that string.
 */
static __inline__ __attribute__((unused))
char *u64toa(uint64_t in)
{
	u64toa_r(in, itoa_buffer);
	return itoa_buffer;
}

static __attribute__((unused))
uintmax_t __strtox(const char *nptr, char **endptr, int base, intmax_t lower_limit, uintmax_t upper_limit)
{
	const char signed_ = lower_limit != 0;
	unsigned char neg = 0, overflow = 0;
	uintmax_t val = 0, limit, old_val;
	char c;

	if (base < 0 || base > 36) {
		SET_ERRNO(EINVAL);
		goto out;
	}

	while (isspace(*nptr))
		nptr++;

	if (*nptr == '+') {
		nptr++;
	} else if (*nptr == '-') {
		neg = 1;
		nptr++;
	}

	if (signed_ && neg)
		limit = -(uintmax_t)lower_limit;
	else
		limit = upper_limit;

	if ((base == 0 || base == 16) &&
	    (strncmp(nptr, "0x", 2) == 0 || strncmp(nptr, "0X", 2) == 0)) {
		base = 16;
		nptr += 2;
	} else if (base == 0 && strncmp(nptr, "0", 1) == 0) {
		base = 8;
		nptr += 1;
	} else if (base == 0) {
		base = 10;
	}

	while (*nptr) {
		c = *nptr;

		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'z')
			c = c - 'a' + 10;
		else if (c >= 'A' && c <= 'Z')
			c = c - 'A' + 10;
		else
			goto out;

		if (c >= base)
			goto out;

		nptr++;
		old_val = val;
		val *= base;
		val += c;

		if (val > limit || val < old_val)
			overflow = 1;
	}

out:
	if (overflow) {
		SET_ERRNO(ERANGE);
		val = limit;
	}
	if (endptr)
		*endptr = (char *)nptr;
	return neg ? -val : val;
}

static __attribute__((unused))
long strtol(const char *nptr, char **endptr, int base)
{
	return __strtox(nptr, endptr, base, LONG_MIN, LONG_MAX);
}

static __attribute__((unused))
unsigned long strtoul(const char *nptr, char **endptr, int base)
{
	return __strtox(nptr, endptr, base, 0, ULONG_MAX);
}

static __attribute__((unused))
long long strtoll(const char *nptr, char **endptr, int base)
{
	return __strtox(nptr, endptr, base, LLONG_MIN, LLONG_MAX);
}

static __attribute__((unused))
unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
	return __strtox(nptr, endptr, base, 0, ULLONG_MAX);
}

static __attribute__((unused))
intmax_t strtoimax(const char *nptr, char **endptr, int base)
{
	return __strtox(nptr, endptr, base, INTMAX_MIN, INTMAX_MAX);
}

static __attribute__((unused))
uintmax_t strtoumax(const char *nptr, char **endptr, int base)
{
	return __strtox(nptr, endptr, base, 0, UINTMAX_MAX);
}

#endif /* _NOLIBC_STDLIB_H */
