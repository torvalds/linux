#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_


#include <linux/compiler.h>	/* for inline */
#include <linux/types.h>	/* for size_t */
#include <linux/stddef.h>	/* for NULL */
#include <stdarg.h>
#include <uapi/linux/string.h>

extern char *strndup_user(const char __user *, long);
extern void *memdup_user(const void __user *, size_t);
extern void *memdup_user_nul(const void __user *, size_t);

/*
 * Include machine specific inline routines
 */
#include <asm/string.h>

#ifndef __HAVE_ARCH_STRCPY
extern char * strcpy(char *,const char *) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_STRNCPY
extern char * strncpy(char *,const char *, __kernel_size_t) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_STRLCPY
size_t strlcpy(char *, const char *, size_t) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_STRSCPY
ssize_t __must_check strscpy(char *, const char *, size_t);
#endif
#ifndef __HAVE_ARCH_STRCAT
extern char * strcat(char *, const char *) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_STRNCAT
extern char * strncat(char *, const char *, __kernel_size_t) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_STRLCAT
extern size_t strlcat(char *, const char *, __kernel_size_t) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_STRCMP
extern int strcmp(const char *,const char *) __nocapture();
#endif
#ifndef __HAVE_ARCH_STRNCMP
extern int strncmp(const char *,const char *,__kernel_size_t) __nocapture(1, 2);
#endif
#ifndef __HAVE_ARCH_STRCASECMP
extern int strcasecmp(const char *s1, const char *s2) __nocapture();
#endif
#ifndef __HAVE_ARCH_STRNCASECMP
extern int
strncasecmp(const char *s1, const char *s2, size_t n) __nocapture(1, 2);
#endif
#ifndef __HAVE_ARCH_STRCHR
extern char * strchr(const char *,int) __nocapture(-1);
#endif
#ifndef __HAVE_ARCH_STRCHRNUL
extern char * strchrnul(const char *,int) __nocapture(-1);
#endif
#ifndef __HAVE_ARCH_STRNCHR
extern char * strnchr(const char *, size_t, int) __nocapture(-1);
#endif
#ifndef __HAVE_ARCH_STRRCHR
extern char * strrchr(const char *,int) __nocapture(-1);
#endif
extern char * __must_check skip_spaces(const char *) __nocapture(-1);

extern char *strim(char *);

static inline __must_check char *strstrip(char *str)
{
	return strim(str);
}

#ifndef __HAVE_ARCH_STRSTR
extern char * strstr(const char *, const char *) __nocapture(-1, 2);
#endif
#ifndef __HAVE_ARCH_STRNSTR
extern char * strnstr(const char *, const char *, size_t) __nocapture(-1, 2);
#endif
#ifndef __HAVE_ARCH_STRLEN
extern __kernel_size_t strlen(const char *) __nocapture(1);
#endif
#ifndef __HAVE_ARCH_STRNLEN
extern __kernel_size_t strnlen(const char *,__kernel_size_t) __nocapture(1);
#endif
#ifndef __HAVE_ARCH_STRPBRK
extern char * strpbrk(const char *,const char *) __nocapture(-1, 2);
#endif
#ifndef __HAVE_ARCH_STRSEP
extern char * strsep(char **,const char *) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_STRSPN
extern __kernel_size_t strspn(const char *,const char *) __nocapture();
#endif
#ifndef __HAVE_ARCH_STRCSPN
extern __kernel_size_t strcspn(const char *,const char *) __nocapture();
#endif

#ifndef __HAVE_ARCH_MEMSET
extern void * memset(void *,int,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCPY
extern void * memcpy(void *,const void *,__kernel_size_t) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_MEMMOVE
extern void * memmove(void *,const void *,__kernel_size_t) __nocapture(2);
#endif
#ifndef __HAVE_ARCH_MEMSCAN
extern void * memscan(void *,int,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCMP
extern int memcmp(const void *,const void *,__kernel_size_t) __nocapture(1, 2);
#endif
#ifndef __HAVE_ARCH_MEMCHR
extern void * memchr(const void *,int,__kernel_size_t) __nocapture(-1);
#endif
void *memchr_inv(const void *s, int c, size_t n) __nocapture(-1);
char *strreplace(char *s, char old, char new);

extern void kfree_const(const void *x);

extern char *kstrdup(const char *s, gfp_t gfp) __malloc __nocapture(1);
extern const char *kstrdup_const(const char *s, gfp_t gfp) __nocapture(1);
extern char *kstrndup(const char *s, size_t len, gfp_t gfp) __nocapture(1);
extern void *kmemdup(const void *src, size_t len, gfp_t gfp) __nocapture(1);

extern char **argv_split(gfp_t gfp, const char *str, int *argcp);
extern void argv_free(char **argv);

extern bool sysfs_streq(const char *s1, const char *s2) __nocapture();
extern int kstrtobool(const char *s, bool *res) __nocapture(1);
static inline int strtobool(const char *s, bool *res)
{
	return kstrtobool(s, res);
}

int match_string(const char * const *array, size_t n, const char *string);

#ifdef CONFIG_BINARY_PRINTF
int vbin_printf(u32 *bin_buf, size_t size, const char *fmt,
		va_list args) __nocapture(3);
int bstr_printf(char *buf, size_t size, const char *fmt,
		const u32 *bin_buf) __nocapture(3);
int bprintf(u32 *bin_buf, size_t size, const char *fmt, ...) __printf(3, 4);
#endif

extern ssize_t memory_read_from_buffer(void *to, size_t count, loff_t *ppos,
				       const void *from, size_t available);

/**
 * strstarts - does @str start with @prefix?
 * @str: string to examine
 * @prefix: prefix to look for.
 */
static inline bool strstarts(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

size_t memweight(const void *ptr, size_t bytes);
void memzero_explicit(void *s, size_t count);

/**
 * kbasename - return the last part of a pathname.
 *
 * @path: path to extract the filename from.
 */
static inline const char *kbasename(const char *path)
{
	const char *tail = strrchr(path, '/');
	return tail ? tail + 1 : path;
}

#endif /* _LINUX_STRING_H_ */
