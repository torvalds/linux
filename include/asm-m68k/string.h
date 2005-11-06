#ifndef _M68K_STRING_H_
#define _M68K_STRING_H_

#include <asm/setup.h>
#include <asm/page.h>

#define __HAVE_ARCH_STRCPY
static inline char * strcpy(char * dest,const char *src)
{
  char *xdest = dest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
        "jne 1b"
	: "=a" (dest), "=a" (src)
        : "0" (dest), "1" (src) : "memory");
  return xdest;
}

#define __HAVE_ARCH_STRNCPY
static inline char * strncpy(char *dest, const char *src, size_t n)
{
  char *xdest = dest;

  if (n == 0)
    return xdest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
	"jeq 2f\n\t"
        "subql #1,%2\n\t"
        "jne 1b\n\t"
        "2:"
        : "=a" (dest), "=a" (src), "=d" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "memory");
  return xdest;
}

#define __HAVE_ARCH_STRCAT
static inline char * strcat(char * dest, const char * src)
{
	char *tmp = dest;

	while (*dest)
		dest++;
	while ((*dest++ = *src++))
		;

	return tmp;
}

#define __HAVE_ARCH_STRNCAT
static inline char * strncat(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	if (count) {
		while (*dest)
			dest++;
		while ((*dest++ = *src++)) {
			if (--count == 0) {
				*dest++='\0';
				break;
			}
		}
	}

	return tmp;
}

#define __HAVE_ARCH_STRCHR
static inline char * strchr(const char * s, int c)
{
  const char ch = c;

  for(; *s != ch; ++s)
    if (*s == '\0')
      return( NULL );
  return( (char *) s);
}

/* strstr !! */

#define __HAVE_ARCH_STRLEN
static inline size_t strlen(const char * s)
{
  const char *sc;
  for (sc = s; *sc != '\0'; ++sc) ;
  return(sc - s);
}

/* strnlen !! */

#define __HAVE_ARCH_STRCMP
static inline int strcmp(const char * cs,const char * ct)
{
  char __res;

  __asm__
       ("1:\tmoveb %0@+,%2\n\t" /* get *cs */
        "cmpb %1@+,%2\n\t"      /* compare a byte */
        "jne  2f\n\t"           /* not equal, break out */
        "tstb %2\n\t"           /* at end of cs? */
        "jne  1b\n\t"           /* no, keep going */
        "jra  3f\n\t"		/* strings are equal */
        "2:\tsubb %1@-,%2\n\t"  /* *cs - *ct */
        "3:"
        : "=a" (cs), "=a" (ct), "=d" (__res)
        : "0" (cs), "1" (ct));
  return __res;
}

#define __HAVE_ARCH_STRNCMP
static inline int strncmp(const char * cs,const char * ct,size_t count)
{
  char __res;

  if (!count)
    return 0;
  __asm__
       ("1:\tmovb %0@+,%3\n\t"          /* get *cs */
        "cmpb   %1@+,%3\n\t"            /* compare a byte */
        "jne    3f\n\t"                 /* not equal, break out */
        "tstb   %3\n\t"                 /* at end of cs? */
        "jeq    4f\n\t"                 /* yes, all done */
        "subql  #1,%2\n\t"              /* no, adjust count */
        "jne    1b\n\t"                 /* more to do, keep going */
        "2:\tmoveq #0,%3\n\t"           /* strings are equal */
        "jra    4f\n\t"
        "3:\tsubb %1@-,%3\n\t"          /* *cs - *ct */
        "4:"
        : "=a" (cs), "=a" (ct), "=d" (count), "=d" (__res)
        : "0" (cs), "1" (ct), "2" (count));
  return __res;
}

#define __HAVE_ARCH_MEMSET
extern void *memset(void *, int, __kernel_size_t);
#define memset(d, c, n) __builtin_memset(d, c, n)

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *, const void *, __kernel_size_t);
#define memcpy(d, s, n) __builtin_memcpy(d, s, n)

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMCMP
extern int memcmp(const void *, const void *, __kernel_size_t);
#define memcmp(d, s, n) __builtin_memcmp(d, s, n)

#endif /* _M68K_STRING_H_ */
