/*
 * util.c -- set of various support routines.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif /* HAVE_SCHED_H */
#ifdef HAVE_SYS_CPUSET_H
#include <sys/cpuset.h>
#endif /* HAVE_SYS_CPUSET_H */
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif /* HAVE_SYSLOG_H */
#include <unistd.h>
#ifdef HAVE_SYS_RANDOM_H
#include <sys/random.h>
#endif

#include "util.h"
#include "region-allocator.h"
#include "dname.h"
#include "namedb.h"
#include "rdata.h"
#include "zonec.h"
#include "nsd.h"
#include "options.h"

#ifdef USE_MMAP_ALLOC
#include <sys/mman.h>

#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define	MAP_ANONYMOUS	MAP_ANON
#elif defined(MAP_ANONYMOUS) && !defined(MAP_ANON)
#define	MAP_ANON	MAP_ANONYMOUS
#endif

#endif /* USE_MMAP_ALLOC */

#ifndef NDEBUG
unsigned nsd_debug_facilities = 0xffff;
int nsd_debug_level = 0;
#endif

#define MSB_32 0x80000000

int verbosity = 0;

static const char *global_ident = NULL;
static log_function_type *current_log_function = log_file;
static FILE *current_log_file = NULL;
int log_time_asc = 1;
int log_time_iso = 0;

#ifdef USE_LOG_PROCESS_ROLE
void
log_set_process_role(const char *process_role)
{
	global_ident = process_role;
}
#endif

void
log_init(const char *ident)
{
	global_ident = ident;
	current_log_file = stderr;
}

void
log_open(int option, int facility, const char *filename)
{
#ifdef HAVE_SYSLOG_H
	openlog(global_ident, option, facility);
#endif /* HAVE_SYSLOG_H */
	if (filename) {
		FILE *file = fopen(filename, "a");
		if (!file) {
			log_msg(LOG_ERR, "Cannot open %s for appending (%s), "
					 "logging to stderr",
				filename, strerror(errno));
		} else {
			current_log_file = file;
		}
	}
}

void
log_reopen(const char *filename, uint8_t verbose)
{
	if (filename) {
		FILE *file;
		if(strcmp(filename, "/dev/stdout")==0 || strcmp(filename, "/dev/stderr")==0)
			return;
		file = fopen(filename, "a");
		if (!file) {
			if (verbose)
				VERBOSITY(2, (LOG_WARNING,
                                	"Cannot reopen %s for appending (%s), "
					"keeping old logfile",
					filename, strerror(errno)));
		} else {
			if (current_log_file && current_log_file != stderr)
				fclose(current_log_file);
			current_log_file = file;
		}
	}
}

void
log_finalize(void)
{
#ifdef HAVE_SYSLOG_H
	closelog();
#endif /* HAVE_SYSLOG_H */
	if (current_log_file && current_log_file != stderr) {
		fclose(current_log_file);
	}
	current_log_file = NULL;
}

static lookup_table_type log_priority_table[] = {
	{ LOG_ERR, "error" },
	{ LOG_WARNING, "warning" },
	{ LOG_NOTICE, "notice" },
	{ LOG_INFO, "info" },
	{ 0, NULL }
};

void
log_file(int priority, const char *message)
{
	size_t length;
	lookup_table_type *priority_info;
	const char *priority_text = "unknown";

	assert(global_ident);
	assert(current_log_file);

	priority_info = lookup_by_id(log_priority_table, priority);
	if (priority_info) {
		priority_text = priority_info->name;
	}

	/* Bug #104, add time_t timestamp */
#if defined(HAVE_STRFTIME) && defined(HAVE_LOCALTIME_R)
	if(log_time_asc) {
		struct timeval tv;
		char tmbuf[32];
		tmbuf[0]=0;
		tv.tv_usec = 0;
		if(log_time_iso) {
			char tzbuf[16];
			tzbuf[0]=0;
			/* log time in iso format */
			if(gettimeofday(&tv, NULL) == 0) {
				struct tm tm, *tm_p;
				time_t now = (time_t)tv.tv_sec;
				tm_p = localtime_r(&now, &tm);
				strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%dT%H:%M:%S",
					tm_p);
				if(strftime(tzbuf, sizeof(tzbuf), "%z", tm_p) == 5) {
					/* put ':' in "+hh:mm" */
					tzbuf[5] = tzbuf[4];
					tzbuf[4] = tzbuf[3];
					tzbuf[3] = ':';
					tzbuf[6] = 0;
				}
			}
			fprintf(current_log_file, "%s.%3.3d%s %s[%d]: %s: %s",
				tmbuf, (int)tv.tv_usec/1000, tzbuf,
				global_ident, (int) getpid(), priority_text, message);
		} else {
			/* log time in ascii format */
			if(gettimeofday(&tv, NULL) == 0) {
				struct tm tm;
				time_t now = (time_t)tv.tv_sec;
				strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S",
					localtime_r(&now, &tm));
			}
			fprintf(current_log_file, "[%s.%3.3d] %s[%d]: %s: %s",
				tmbuf, (int)tv.tv_usec/1000,
				global_ident, (int) getpid(), priority_text, message);
		}
 	} else
#endif /* have time functions */
		fprintf(current_log_file, "[%d] %s[%d]: %s: %s",
		(int)time(NULL), global_ident, (int) getpid(), priority_text, message);
	length = strlen(message);
	if (length == 0 || message[length - 1] != '\n') {
		fprintf(current_log_file, "\n");
	}
	fflush(current_log_file);
}

void
log_syslog(int priority, const char *message)
{
#ifdef HAVE_SYSLOG_H
	syslog(priority, "%s", message);
#endif /* !HAVE_SYSLOG_H */
	log_file(priority, message);
}

void
log_only_syslog(int priority, const char *message)
{
#ifdef HAVE_SYSLOG_H
	syslog(priority, "%s", message);
#else /* !HAVE_SYSLOG_H */
	/* no syslog, use stderr */
	log_file(priority, message);
#endif
}

void
log_set_log_function(log_function_type *log_function)
{
	current_log_function = log_function;
}

void
log_msg(int priority, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	log_vmsg(priority, format, args);
	va_end(args);
}

void
log_vmsg(int priority, const char *format, va_list args)
{
	char message[MAXSYSLOGMSGLEN];
	vsnprintf(message, sizeof(message), format, args);
	current_log_function(priority, message);
}

void
set_bit(uint8_t bits[], size_t index)
{
	/*
	 * The bits are counted from left to right, so bit #0 is the
	 * left most bit.
	 */
	bits[index / 8] |= (1 << (7 - index % 8));
}

void
clear_bit(uint8_t bits[], size_t index)
{
	/*
	 * The bits are counted from left to right, so bit #0 is the
	 * left most bit.
	 */
	bits[index / 8] &= ~(1 << (7 - index % 8));
}

int
get_bit(uint8_t bits[], size_t index)
{
	/*
	 * The bits are counted from left to right, so bit #0 is the
	 * left most bit.
	 */
	return bits[index / 8] & (1 << (7 - index % 8));
}

lookup_table_type *
lookup_by_name(lookup_table_type *table, const char *name)
{
	while (table->name != NULL) {
		if (strcasecmp(name, table->name) == 0)
			return table;
		table++;
	}
	return NULL;
}

lookup_table_type *
lookup_by_id(lookup_table_type *table, int id)
{
	while (table->name != NULL) {
		if (table->id == id)
			return table;
		table++;
	}
	return NULL;
}

char *
xstrdup(const char *src)
{
	char *result = strdup(src);

	if(!result) {
		log_msg(LOG_ERR, "strdup failed: %s", strerror(errno));
		exit(1);
	}

	return result;
}

void *
xalloc(size_t size)
{
	void *result = malloc(size);

	if (!result) {
		log_msg(LOG_ERR, "malloc failed: %s", strerror(errno));
		exit(1);
	}
	return result;
}

void *
xmallocarray(size_t num, size_t size)
{
        void *result = reallocarray(NULL, num, size);

        if (!result) {
                log_msg(LOG_ERR, "reallocarray failed: %s", strerror(errno));
                exit(1);
        }
        return result;
}

void *
xalloc_zero(size_t size)
{
	void *result = calloc(1, size);
	if (!result) {
		log_msg(LOG_ERR, "calloc failed: %s", strerror(errno));
		exit(1);
	}
	return result;
}

void *
xalloc_array_zero(size_t num, size_t size)
{
	void *result = calloc(num, size);
	if (!result) {
		log_msg(LOG_ERR, "calloc failed: %s", strerror(errno));
		exit(1);
	}
	return result;
}

void *
xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr) {
		log_msg(LOG_ERR, "realloc failed: %s", strerror(errno));
		exit(1);
	}
	return ptr;
}

#ifdef USE_MMAP_ALLOC

void *
mmap_alloc(size_t size)
{
	void *base;

	size += MMAP_ALLOC_HEADER_SIZE;
#ifdef HAVE_MMAP
	base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		log_msg(LOG_ERR, "mmap failed: %s", strerror(errno));
		exit(1);
	}
#else /* !HAVE_MMAP */
	log_msg(LOG_ERR, "mmap failed: don't have mmap");
	exit(1);
#endif /* HAVE_MMAP */

	*((size_t*) base) = size;
	return (void*)((uintptr_t)base + MMAP_ALLOC_HEADER_SIZE);
}


void
mmap_free(void *ptr)
{
	void *base;
	size_t size;

	if (!ptr) return;

	base = (void*)((uintptr_t)ptr - MMAP_ALLOC_HEADER_SIZE);
	size = *((size_t*) base);

#ifdef HAVE_MUNMAP
	if (munmap(base, size) == -1) {
		log_msg(LOG_ERR, "munmap failed: %s", strerror(errno));
		exit(1);
	}
#else /* !HAVE_MUNMAP */
	log_msg(LOG_ERR, "munmap failed: don't have munmap");
	exit(1);
#endif /* HAVE_MUNMAP */
}

#endif /* USE_MMAP_ALLOC */

int
write_data(FILE *file, const void *data, size_t size)
{
	size_t result;

	if (size == 0)
		return 1;

	result = fwrite(data, 1, size, file);

	if (result == 0) {
		log_msg(LOG_ERR, "write failed: %s", strerror(errno));
		return 0;
	} else if (result < size) {
		log_msg(LOG_ERR, "short write (disk full?)");
		return 0;
	} else {
		return 1;
	}
}

int
write_socket(int s, const void *buf, size_t size)
{
	const char* data = (const char*)buf;
	size_t total_count = 0;

	while (total_count < size) {
		ssize_t count
			= write(s, data + total_count, size - total_count);
		if (count == -1) {
			if (errno != EAGAIN && errno != EINTR) {
				return 0;
			} else {
				continue;
			}
		}
		total_count += count;
	}
	return 1;
}

void get_time(struct timespec* t)
{
	struct timeval tv;
#ifdef HAVE_CLOCK_GETTIME
	/* first try nanosecond precision */
	if(clock_gettime(CLOCK_REALTIME, t)>=0) {
		return; /* success */
	}
	log_msg(LOG_ERR, "clock_gettime: %s", strerror(errno));
#endif
	/* try millisecond precision */
	if(gettimeofday(&tv, NULL)>=0) {
		t->tv_sec = tv.tv_sec;
		t->tv_nsec = tv.tv_usec*1000;
		return; /* success */
	}
	log_msg(LOG_ERR, "gettimeofday: %s", strerror(errno));
	/* whole seconds precision */
	t->tv_sec = time(0);
	t->tv_nsec = 0;
}

int
timespec_compare(const struct timespec *left,
		 const struct timespec *right)
{
	/* Compare seconds.  */
	if (left->tv_sec < right->tv_sec) {
		return -1;
	} else if (left->tv_sec > right->tv_sec) {
		return 1;
	} else {
		/* Seconds are equal, compare nanoseconds.  */
		if (left->tv_nsec < right->tv_nsec) {
			return -1;
		} else if (left->tv_nsec > right->tv_nsec) {
			return 1;
		} else {
			return 0;
		}
	}
}


/* One second is 1e9 nanoseconds.  */
#define NANOSECONDS_PER_SECOND   1000000000L

void
timespec_add(struct timespec *left,
	     const struct timespec *right)
{
	left->tv_sec += right->tv_sec;
	left->tv_nsec += right->tv_nsec;
	if (left->tv_nsec >= NANOSECONDS_PER_SECOND) {
		/* Carry.  */
		++left->tv_sec;
		left->tv_nsec -= NANOSECONDS_PER_SECOND;
	}
}

void
timespec_subtract(struct timespec *left,
		  const struct timespec *right)
{
	left->tv_sec -= right->tv_sec;
	left->tv_nsec -= right->tv_nsec;
	if (left->tv_nsec < 0L) {
		/* Borrow.  */
		--left->tv_sec;
		left->tv_nsec += NANOSECONDS_PER_SECOND;
	}
}

ssize_t
hex_ntop(uint8_t const *src, size_t srclength, char *target, size_t targsize)
{
	static char hexdigits[] = {
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};
	size_t i;

	if (targsize < srclength * 2 + 1) {
		return -1;
	}

	for (i = 0; i < srclength; ++i) {
		*target++ = hexdigits[src[i] >> 4U];
		*target++ = hexdigits[src[i] & 0xfU];
	}
	*target = '\0';
	return 2 * srclength;
}

ssize_t
hex_pton(const char* src, uint8_t* target, size_t targsize)
{
	uint8_t *t = target;
	if(strlen(src) % 2 != 0 || strlen(src)/2 > targsize) {
		return -1;
	}
	while(*src) {
		if(!isxdigit((unsigned char)src[0]) ||
			!isxdigit((unsigned char)src[1]))
			return -1;
		*t++ = hexdigit_to_int(src[0]) * 16 +
			hexdigit_to_int(src[1]) ;
		src += 2;
	}
	return t-target;
}

int
b32_ntop(uint8_t const *src, size_t srclength, char *target, size_t targsize)
{
	static char b32[]="0123456789abcdefghijklmnopqrstuv";
	char buf[9];
	ssize_t len=0;

	while(srclength > 0)
	{
		int t;
		memset(buf,'\0',sizeof buf);

		/* xxxxx000 00000000 00000000 00000000 00000000 */
		buf[0]=b32[src[0] >> 3];

		/* 00000xxx xx000000 00000000 00000000 00000000 */
		t=(src[0]&7) << 2;
		if(srclength > 1)
			t+=src[1] >> 6;
		buf[1]=b32[t];
		if(srclength == 1)
			break;

		/* 00000000 00xxxxx0 00000000 00000000 00000000 */
		buf[2]=b32[(src[1] >> 1)&0x1f];

		/* 00000000 0000000x xxxx0000 00000000 00000000 */
		t=(src[1]&1) << 4;
		if(srclength > 2)
			t+=src[2] >> 4;
		buf[3]=b32[t];
		if(srclength == 2)
			break;

		/* 00000000 00000000 0000xxxx x0000000 00000000 */
		t=(src[2]&0xf) << 1;
		if(srclength > 3)
			t+=src[3] >> 7;
		buf[4]=b32[t];
		if(srclength == 3)
			break;

		/* 00000000 00000000 00000000 0xxxxx00 00000000 */
		buf[5]=b32[(src[3] >> 2)&0x1f];

		/* 00000000 00000000 00000000 000000xx xxx00000 */
		t=(src[3]&3) << 3;
		if(srclength > 4)
			t+=src[4] >> 5;
		buf[6]=b32[t];
		if(srclength == 4)
			break;

		/* 00000000 00000000 00000000 00000000 000xxxxx */
		buf[7]=b32[src[4]&0x1f];

		if(targsize < 8)
			return -1;

		src += 5;
		srclength -= 5;

		memcpy(target,buf,8);
		target += 8;
		targsize -= 8;
		len += 8;
	}
	if(srclength)
	{
		size_t tlen = strlcpy(target, buf, targsize);
		if (tlen >= targsize)
			return -1;
		len += tlen;
	}
	else if(targsize < 1)
		return -1;
	else
		*target='\0';
	return len;
}

int
b32_pton(const char *src, uint8_t *target, size_t tsize)
{
	char ch;
	size_t p=0;

	memset(target,'\0',tsize);
	while((ch = *src++)) {
		uint8_t d;
		size_t b;
		size_t n;

		if(p+5 >= tsize*8)
		       return -1;

		if(isspace((unsigned char)ch))
			continue;

		if(ch >= '0' && ch <= '9')
			d=ch-'0';
		else if(ch >= 'A' && ch <= 'V')
			d=ch-'A'+10;
		else if(ch >= 'a' && ch <= 'v')
			d=ch-'a'+10;
		else
			return -1;

		b=7-p%8;
		n=p/8;

		if(b >= 4)
			target[n]|=d << (b-4);
		else {
			target[n]|=d >> (4-b);
			target[n+1]|=d << (b+4);
		}
		p+=5;
	}
	return (p+7)/8;
}

void
strip_string(char *str)
{
	char *start = str;
	char *end = str + strlen(str) - 1;

	while (isspace((unsigned char)*start))
		++start;
	if (start > end) {
		/* Completely blank. */
		str[0] = '\0';
	} else {
		while (isspace((unsigned char)*end))
			--end;
		*++end = '\0';

		if (str != start)
			memmove(str, start, end - start + 1);
	}
}

int
hexdigit_to_int(char ch)
{
	switch (ch) {
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'a': case 'A': return 10;
	case 'b': case 'B': return 11;
	case 'c': case 'C': return 12;
	case 'd': case 'D': return 13;
	case 'e': case 'E': return 14;
	case 'f': case 'F': return 15;
	default:
		abort();
	}
}

/* code to calculate CRC. Lifted from BSD 4.4 crc.c in cksum(1). BSD license.
   http://www.tsfr.org/~orc/Code/bsd/bsd-current/cksum/crc.c.
   or http://gobsd.com/code/freebsd/usr.bin/cksum/crc.c
   The polynomial is 0x04c11db7L. */
static uint32_t crctab[] = {
	0x0,
	0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6,
	0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
	0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac,
	0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f,
	0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a,
	0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58,
	0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033,
	0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe,
	0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
	0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4,
	0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5,
	0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
	0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
	0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c,
	0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1,
	0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b,
	0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698,
	0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d,
	0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
	0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f,
	0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
	0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80,
	0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
	0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a,
	0x58c1663d, 0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629,
	0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c,
	0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e,
	0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65,
	0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
	0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
	0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2,
	0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74,
	0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
	0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c, 0x7b827d21,
	0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a,
	0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087,
	0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d,
	0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce,
	0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb,
	0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
	0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09,
	0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf,
	0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

#define	COMPUTE(var, ch)	(var) = (var) << 8 ^ crctab[(var) >> 24 ^ (ch)]

uint32_t
compute_crc(uint32_t crc, uint8_t* data, size_t len)
{
	size_t i;
	for(i=0; i<len; ++i)
		COMPUTE(crc, data[i]);
	return crc;
}

int
write_data_crc(FILE *file, const void *data, size_t size, uint32_t* crc)
{
	int ret = write_data(file, data, size);
	*crc = compute_crc(*crc, (uint8_t*)data, size);
	return ret;
}

#define SERIAL_BITS      32
int
compare_serial(uint32_t a, uint32_t b)
{
        const uint32_t cutoff = ((uint32_t) 1 << (SERIAL_BITS - 1));

        if (a == b) {
                return 0;
        } else if ((a < b && b - a < cutoff) || (a > b && a - b > cutoff)) {
                return -1;
        } else {
                return 1;
        }
}

uint16_t
qid_generate(void)
{
#ifdef HAVE_GETRANDOM
	uint16_t r;
	if(getrandom(&r, sizeof(r), 0) == -1) {
		log_msg(LOG_ERR, "getrandom failed: %s", strerror(errno));
		exit(1);
	}
	return r;
#elif defined(HAVE_ARC4RANDOM)
    /* arc4random_uniform not needed because range is a power of 2 */
    return (uint16_t) arc4random();
#else
    return (uint16_t) random();
#endif
}

int
random_generate(int max)
{
#ifdef HAVE_GETRANDOM
	int r;
	if(getrandom(&r, sizeof(r), 0) == -1) {
		log_msg(LOG_ERR, "getrandom failed: %s", strerror(errno));
		exit(1);
	}
	return (int)(((unsigned)r)%max);
#elif defined(HAVE_ARC4RANDOM_UNIFORM)
    return (int) arc4random_uniform(max);
#elif defined(HAVE_ARC4RANDOM)
    return (int) (arc4random() % max);
#else
    return (int) ((unsigned)random() % max);
#endif
}

void
cleanup_region(void *data)
{
	region_type *region = (region_type *) data;
	region_destroy(region);
}

struct state_pretty_rr*
create_pretty_rr(struct region* region)
{
	struct state_pretty_rr* state = (struct state_pretty_rr*)
		region_alloc(region, sizeof(struct state_pretty_rr));
	state->previous_owner_region = region_create(xalloc, free);
	state->previous_owner = NULL;
	state->previous_owner_origin = NULL;
        region_add_cleanup(region, cleanup_region,
		state->previous_owner_region);
	return state;
}

static void
set_previous_owner(struct state_pretty_rr *state, const dname_type *dname)
{
	region_free_all(state->previous_owner_region);
	state->previous_owner = dname_copy(state->previous_owner_region, dname);
	state->previous_owner_origin = dname_origin(
		state->previous_owner_region, state->previous_owner);
}

int
print_rr(FILE *out,
         struct state_pretty_rr *state,
         rr_type *record,
	 region_type* rr_region,
	 buffer_type* output)
{
        rrtype_descriptor_type *descriptor
                = rrtype_descriptor_by_type(record->type);
        int result;
        const dname_type *owner = domain_dname(record->owner);
	buffer_clear(output);
        if (state) {
		if (!state->previous_owner
			|| dname_compare(state->previous_owner, owner) != 0) {
			const dname_type *owner_origin
				= dname_origin(rr_region, owner);
			int origin_changed = (!state->previous_owner_origin
				|| dname_compare(state->previous_owner_origin,
				   owner_origin) != 0);
			if (origin_changed) {
				buffer_printf(output, "$ORIGIN %s\n",
					dname_to_string(owner_origin, NULL));
			}

			set_previous_owner(state, owner);
			buffer_printf(output, "%s",
				dname_to_string(owner,
					state->previous_owner_origin));
			region_free_all(rr_region);
		}
	} else {
		buffer_printf(output, "%s", dname_to_string(owner, NULL));
	}

	buffer_printf(output, "\t%lu\t%s\t%s",
		(unsigned long) record->ttl,
		rrclass_to_string(record->klass),
		rrtype_to_string(record->type));

	result = print_rdata(output, descriptor, record);
	if (!result) {
		/*
		 * Some RDATA failed to print, so print the record's
		 * RDATA in unknown format.
		 */
		result = rdata_atoms_to_unknown_string(output,
			descriptor, record->rdata_count, record->rdatas);
	}

	if (result) {
		buffer_printf(output, "\n");
		buffer_flip(output);
		result = write_data(out, buffer_current(output),
		buffer_remaining(output));
	}
	return result;
}

const char*
rcode2str(int rc)
{
	switch(rc) {
		case RCODE_OK:
			return "NO ERROR";
		case RCODE_FORMAT:
			return "FORMAT ERROR";
		case RCODE_SERVFAIL:
			return "SERVFAIL";
		case RCODE_NXDOMAIN:
			return "NAME ERROR";
		case RCODE_IMPL:
			return "NOT IMPL";
		case RCODE_REFUSE:
			return "REFUSED";
		case RCODE_YXDOMAIN:
			return "YXDOMAIN";
		case RCODE_YXRRSET:
			return "YXRRSET";
		case RCODE_NXRRSET:
			return "NXRRSET";
		case RCODE_NOTAUTH:
			return "SERVER NOT AUTHORITATIVE FOR ZONE";
		case RCODE_NOTZONE:
			/* Name not contained in zone */
			return "NOTZONE";
		default:
			return "UNKNOWN ERROR";
	}
	return NULL; /* ENOREACH */
}

void
addr2str(
#ifdef INET6
	struct sockaddr_storage *addr
#else
	struct sockaddr_in *addr
#endif
	, char* str, size_t len)
{
#ifdef INET6
	if (addr->ss_family == AF_INET6) {
		if (!inet_ntop(AF_INET6,
			&((struct sockaddr_in6 *)addr)->sin6_addr, str, len))
			strlcpy(str, "[unknown ip6, inet_ntop failed]", len);
		return;
	}
#endif
	if (!inet_ntop(AF_INET, &((struct sockaddr_in *)addr)->sin_addr,
		str, len))
		strlcpy(str, "[unknown ip4, inet_ntop failed]", len);
}

void
addrport2str(
#ifdef INET6
	struct sockaddr_storage *addr
#else
	struct sockaddr_in *addr
#endif
	, char* str, size_t len)
{
	char ip[256];
#ifdef INET6
	if (addr->ss_family == AF_INET6) {
		if (!inet_ntop(AF_INET6,
			&((struct sockaddr_in6 *)addr)->sin6_addr, ip, sizeof(ip)))
			strlcpy(ip, "[unknown ip6, inet_ntop failed]", sizeof(ip));
		/* append port number */
		snprintf(str, len, "%s@%u", ip,
			(unsigned)ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
		return;
	} else
#endif
	if (!inet_ntop(AF_INET, &((struct sockaddr_in *)addr)->sin_addr,
		ip, sizeof(ip)))
		strlcpy(ip, "[unknown ip4, inet_ntop failed]", sizeof(ip));
	/* append port number */
	snprintf(str, len, "%s@%u", ip,
		(unsigned)ntohs(((struct sockaddr_in *)addr)->sin_port));
}

void
append_trailing_slash(const char** dirname, region_type* region)
{
	int l = strlen(*dirname);
	if (l>0 && (*dirname)[l-1] != '/' && l < 0xffffff) {
		char *dirname_slash = region_alloc(region, l+2);
		memcpy(dirname_slash, *dirname, l+1);
		strlcat(dirname_slash, "/", l+2);
		/* old dirname is leaked, this is only used for chroot, once */
		*dirname = dirname_slash;
	}
}

int
file_inside_chroot(const char* fname, const char* chr)
{
	/* true if filename starts with chroot or is not absolute */
	return ((fname && fname[0] && strncmp(fname, chr, strlen(chr)) == 0) ||
		(fname && fname[0] != '/'));
}

/*
 * Something went wrong, give error messages and exit.
 */
void
error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	log_vmsg(LOG_ERR, format, args);
	va_end(args);
	exit(1);
}

#ifdef HAVE_CPUSET_T
#if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_CONF)
/* exists on Linux and FreeBSD */
int number_of_cpus(void)
{
	return (int)sysconf(_SC_NPROCESSORS_CONF);
}
#else
int number_of_cpus(void)
{
	return -1;
}
#endif
#ifdef __gnu_hurd__
/* HURD has no sched_setaffinity implementation, but links an always fail,
 * with a linker error, we print an error when it is used */
int set_cpu_affinity(cpuset_t *ATTR_UNUSED(set))
{
	log_msg(LOG_ERR, "sched_setaffinity: not available on this system");
	return -1;
}
#elif defined(HAVE_SCHED_SETAFFINITY)
/* Linux */
int set_cpu_affinity(cpuset_t *set)
{
	assert(set != NULL);
	return sched_setaffinity(getpid(), sizeof(*set), set);
}
#else
/* FreeBSD */
int set_cpu_affinity(cpuset_t *set)
{
	assert(set != NULL);
	return cpuset_setaffinity(
		CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(*set), set);
}
#endif
#endif /* HAVE_CPUSET_T */

void add_cookie_secret(struct nsd* nsd, uint8_t* secret)
{
	/* New cookie secret becomes the staging secret (position 1)
	 * unless there is no active cookie yet, then it becomes the active
	 * secret.  If the NSD_COOKIE_HISTORY_SIZE > 2 then all staging cookies
	 * are moved one position down.
	 */
	if(nsd->cookie_count == 0) {
		memcpy( nsd->cookie_secrets->cookie_secret
		       , secret, NSD_COOKIE_SECRET_SIZE);
		nsd->cookie_count = 1;
		explicit_bzero(secret, NSD_COOKIE_SECRET_SIZE);
		return;
	}
#if NSD_COOKIE_HISTORY_SIZE > 2
	memmove( &nsd->cookie_secrets[2], &nsd->cookie_secrets[1]
	       , sizeof(struct cookie_secret) * (NSD_COOKIE_HISTORY_SIZE - 2));
#endif
	memcpy( nsd->cookie_secrets[1].cookie_secret
	      , secret, NSD_COOKIE_SECRET_SIZE);
	nsd->cookie_count = nsd->cookie_count     < NSD_COOKIE_HISTORY_SIZE
	                  ? nsd->cookie_count + 1 : NSD_COOKIE_HISTORY_SIZE;
	explicit_bzero(secret, NSD_COOKIE_SECRET_SIZE);
}

void activate_cookie_secret(struct nsd* nsd)
{
	uint8_t active_secret[NSD_COOKIE_SECRET_SIZE];
	/* The staging secret becomes the active secret.
	 * The active secret becomes a staging secret.
	 * If the NSD_COOKIE_HISTORY_SIZE > 2 then all staging secrets are moved
	 * one position up and the previously active secret becomes the last
	 * staging secret.
	 */
	if(nsd->cookie_count < 2)
		return;
	memcpy( active_secret, nsd->cookie_secrets[0].cookie_secret
	      , NSD_COOKIE_SECRET_SIZE);
	memmove( &nsd->cookie_secrets[0], &nsd->cookie_secrets[1]
	       , sizeof(struct cookie_secret) * (NSD_COOKIE_HISTORY_SIZE - 1));
	memcpy( nsd->cookie_secrets[nsd->cookie_count - 1].cookie_secret
	      , active_secret, NSD_COOKIE_SECRET_SIZE);
	explicit_bzero(active_secret, NSD_COOKIE_SECRET_SIZE);
}

void drop_cookie_secret(struct nsd* nsd)
{
	/* Drops a staging cookie secret. If there are more than one, it will
	 * drop the last staging secret. */
	if(nsd->cookie_count < 2)
		return;
	explicit_bzero( nsd->cookie_secrets[nsd->cookie_count - 1].cookie_secret
	              , NSD_COOKIE_SECRET_SIZE);
	nsd->cookie_count -= 1;
}

void reconfig_cookies(struct nsd* nsd, struct nsd_options* options)
{
	cookie_secret_type cookie_secrets[NSD_COOKIE_HISTORY_SIZE];
	char secret[NSD_COOKIE_SECRET_SIZE * 2 + 2/*'\n' and '\0'*/];
	FILE* f = NULL;
	size_t count = 0;
	const char* fn;
	size_t i, j;

	nsd->do_answer_cookie = options->answer_cookie;

	/* Cookie secrets in the configuration file take precedence */
	if(options->cookie_secret) {
#ifndef NDEBUG
		ssize_t len =
#endif
		hex_pton(options->cookie_secret,
				nsd->cookie_secrets[0].cookie_secret,
				NSD_COOKIE_SECRET_SIZE);
		/* Cookie length guaranteed in configparser.y */
		assert(len == NSD_COOKIE_SECRET_SIZE);
		nsd->cookie_count = 1;
		if(options->cookie_staging_secret) {
#ifndef NDEBUG
			len =
#endif
			hex_pton(options->cookie_staging_secret,
					nsd->cookie_secrets[1].cookie_secret,
					NSD_COOKIE_SECRET_SIZE);
			/* Cookie length guaranteed in configparser.y */
			assert(len == NSD_COOKIE_SECRET_SIZE);
			nsd->cookie_count = 2;
		}
		/*************************************************************/
		nsd->cookie_secrets_source = COOKIE_SECRETS_FROM_CONFIG;
		return;
		/*************************************************************/
	}
	/* Are cookies from file explicitly disabled? */
	if(!(fn = nsd->options->cookie_secret_file))
		goto generate_cookie_secrets;

	else if((f = fopen(fn, "r")) != NULL)
		; /* pass */

	/* a non-existing cookie file is not necessarily an error */
	else if(errno != ENOENT) {
		log_msg( LOG_ERR
		       , "error reading cookie secret file \"%s\": \"%s\""
		       , fn, strerror(errno));
		goto generate_cookie_secrets;

	/* Only at startup cookie_secrets_source == COOKIE_SECRETS_NONE.
	 * Only then the previous default file location will be tried
	 * when the current default file location didn't exist.
	 */
	} else if(nsd->cookie_secrets_source == COOKIE_SECRETS_NONE
	       && nsd->options->cookie_secret_file_is_default
	       && (f = fopen((fn = CONFIGDIR"/nsd_cookiesecrets.txt"),"r")))
		; /* pass */

	else if(errno != ENOENT) {
		log_msg( LOG_ERR
		       , "error reading cookie secret file \"%s\": \"%s\""
		       , fn, strerror(errno));
		goto generate_cookie_secrets;
	} else
		goto generate_cookie_secrets;

	/* cookie secret file exists and is readable */
	for( count = 0; count < NSD_COOKIE_HISTORY_SIZE; count++ ) {
		size_t secret_len = 0;
		ssize_t decoded_len = 0;
		if( fgets(secret, sizeof(secret), f) == NULL ) { break; }
		secret_len = strlen(secret);
		if( secret_len == 0 ) { break; }
		assert( secret_len <= sizeof(secret) );
		secret_len = secret[secret_len - 1] == '\n' ? secret_len - 1 : secret_len;
		if( secret_len != NSD_COOKIE_SECRET_SIZE * 2 ) {
			fclose(f);
			log_msg( LOG_ERR
			       , "error parsing cookie secret file \"%s\""
			       , fn);
			explicit_bzero(cookie_secrets, sizeof(cookie_secrets));
			explicit_bzero(secret, sizeof(secret));
			goto generate_cookie_secrets;
		}
		/* needed for `hex_pton`; stripping potential `\n` */
		secret[secret_len] = '\0';
		decoded_len = hex_pton(secret, cookie_secrets[count].cookie_secret,
		                       NSD_COOKIE_SECRET_SIZE);
		if( decoded_len != NSD_COOKIE_SECRET_SIZE ) {
			fclose(f);
			log_msg( LOG_ERR
			       , "error parsing cookie secret file \"%s\""
			       , fn);
			explicit_bzero(cookie_secrets, sizeof(cookie_secrets));
			explicit_bzero(secret, sizeof(secret));
			goto generate_cookie_secrets;
		}
		explicit_bzero(secret, sizeof(secret));
	}
	fclose(f);
	if(count) {
		nsd->cookie_count = count;
		memcpy(nsd->cookie_secrets, cookie_secrets, sizeof(cookie_secrets));
		region_str_replace(  nsd->region
		                  , &nsd->cookie_secrets_filename, fn );
		explicit_bzero(cookie_secrets, sizeof(cookie_secrets));
		/*************************************************************/
		nsd->cookie_secrets_source = COOKIE_SECRETS_FROM_FILE;
		return;
		/*************************************************************/
	}
	explicit_bzero(cookie_secrets, sizeof(cookie_secrets));

generate_cookie_secrets:
	/* Calculate a new random secret */
	srandom(getpid() ^ time(NULL));

	for( j = 0; j < NSD_COOKIE_HISTORY_SIZE; j++) {
#if defined(HAVE_SSL)
		if (!RAND_status()
		||  !RAND_bytes(nsd->cookie_secrets[j].cookie_secret, NSD_COOKIE_SECRET_SIZE))
#endif
		for (i = 0; i < NSD_COOKIE_SECRET_SIZE; i++)
			nsd->cookie_secrets[j].cookie_secret[i] = random_generate(256);
	}
	nsd->cookie_count = 1;
	/*********************************************************************/
	nsd->cookie_secrets_source = COOKIE_SECRETS_GENERATED;
	/*********************************************************************/
}

ssize_t
print_socket_servers(struct nsd_bitset *bitset, char *buf, size_t bufsz)
{
	/* x and y are the start and end points of a range of set bits */
	/* z is the last unset bit */
	int i, x, y, z, n = (int)(bitset->size);
	char *sep = "";
	size_t off, written_total;
	ssize_t written = 0;

	assert(bufsz != 0);

	off = written_total = 0;
	x = y = z = -1;
	for (i = 0; i <= n; i++) {
		if (i == n || !nsd_bitset_isset(bitset, i)) {
			written = 0;
			if (i == n && x == -1) {
				assert(y == -1);
				assert(z == (n - 1));
				written = snprintf(buf, bufsz, "(none)");
			} else if (y > z) {
				assert(x > z);
				if (x == 0 && y == (n - 1)) {
					assert(z == -1);
					written = snprintf(buf+off, bufsz-off,
						"(all)");
				} else if (x == y) {
					written = snprintf(buf+off, bufsz-off,
						"%s%d", sep, x+1);
				} else if (x == (y - 1)) {
					written = snprintf(buf+off, bufsz-off,
						"%s%d %d", sep, x+1, y+1);
				} else {
					assert(y > (x + 1));
					written = snprintf(buf+off, bufsz-off,
						"%s%d-%d", sep, x+1, y+1);
				}
			}
			z = i;
			if (written > 0) {
				written_total += (size_t)written;
				off = (written_total < bufsz) ? written_total : bufsz - 1;
				sep = " ";
			} else if (written < 0) {
				return -1;
			}
		} else if (x <= z) {
			x = y = i;
		} else {
			assert(x > z);
			y = i;
		}
	}
	return written_total;
}
