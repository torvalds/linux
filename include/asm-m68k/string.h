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

#if 0
#define __HAVE_ARCH_STRPBRK
static inline char *strpbrk(const char *cs,const char *ct)
{
  const char *sc1,*sc2;

  for( sc1 = cs; *sc1 != '\0'; ++sc1)
    for( sc2 = ct; *sc2 != '\0'; ++sc2)
      if (*sc1 == *sc2)
	return((char *) sc1);
  return( NULL );
}
#endif

#if 0
#define __HAVE_ARCH_STRSPN
static inline size_t strspn(const char *s, const char *accept)
{
  const char *p;
  const char *a;
  size_t count = 0;

  for (p = s; *p != '\0'; ++p)
    {
      for (a = accept; *a != '\0'; ++a)
        if (*p == *a)
          break;
      if (*a == '\0')
        return count;
      else
        ++count;
    }

  return count;
}
#endif

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
/*
 * This is really ugly, but its highly optimizatiable by the
 * compiler and is meant as compensation for gcc's missing
 * __builtin_memset(). For the 680[23]0	it might be worth considering
 * the optimal number of misaligned writes compared to the number of
 * tests'n'branches needed to align the destination address. The
 * 680[46]0 doesn't really care due to their copy-back caches.
 *						10/09/96 - Jes Sorensen
 */
static inline void * __memset_g(void * s, int c, size_t count)
{
  void *xs = s;
  size_t temp;

  if (!count)
    return xs;

  c &= 0xff;
  c |= c << 8;
  c |= c << 16;

  if (count < 36){
	  long *ls = s;

	  switch(count){
	  case 32: case 33: case 34: case 35:
		  *ls++ = c;
	  case 28: case 29: case 30: case 31:
		  *ls++ = c;
	  case 24: case 25: case 26: case 27:
		  *ls++ = c;
	  case 20: case 21: case 22: case 23:
		  *ls++ = c;
	  case 16: case 17: case 18: case 19:
		  *ls++ = c;
	  case 12: case 13: case 14: case 15:
		  *ls++ = c;
	  case 8: case 9: case 10: case 11:
		  *ls++ = c;
	  case 4: case 5: case 6: case 7:
		  *ls++ = c;
		  break;
	  default:
		  break;
	  }
	  s = ls;
	  if (count & 0x02){
		  short *ss = s;
		  *ss++ = c;
		  s = ss;
	  }
	  if (count & 0x01){
		  char *cs = s;
		  *cs++ = c;
		  s = cs;
	  }
	  return xs;
  }

  if ((long) s & 1)
    {
      char *cs = s;
      *cs++ = c;
      s = cs;
      count--;
    }
  if (count > 2 && (long) s & 2)
    {
      short *ss = s;
      *ss++ = c;
      s = ss;
      count -= 2;
    }
  temp = count >> 2;
  if (temp)
    {
      long *ls = s;
      temp--;
      do
	*ls++ = c;
      while (temp--);
      s = ls;
    }
  if (count & 2)
    {
      short *ss = s;
      *ss++ = c;
      s = ss;
    }
  if (count & 1)
    {
      char *cs = s;
      *cs = c;
    }
  return xs;
}

/*
 * __memset_page assumes that data is longword aligned. Most, if not
 * all, of these page sized memsets are performed on page aligned
 * areas, thus we do not need to check if the destination is longword
 * aligned. Of course we suffer a serious performance loss if this is
 * not the case but I think the risk of this ever happening is
 * extremely small. We spend a lot of time clearing pages in
 * get_empty_page() so I think it is worth it anyway. Besides, the
 * 680[46]0 do not really care about misaligned writes due to their
 * copy-back cache.
 *
 * The optimized case for the 680[46]0 is implemented using the move16
 * instruction. My tests showed that this implementation is 35-45%
 * faster than the original implementation using movel, the only
 * caveat is that the destination address must be 16-byte aligned.
 *                                            01/09/96 - Jes Sorensen
 */
static inline void * __memset_page(void * s,int c,size_t count)
{
  unsigned long data, tmp;
  void *xs = s;

  c = c & 255;
  data = c | (c << 8);
  data |= data << 16;

#ifdef CPU_M68040_OR_M68060_ONLY

  if (((unsigned long) s) & 0x0f)
	  __memset_g(s, c, count);
  else{
	  unsigned long *sp = s;
	  *sp++ = data;
	  *sp++ = data;
	  *sp++ = data;
	  *sp++ = data;

	  __asm__ __volatile__("1:\t"
			       ".chip 68040\n\t"
			       "move16 %2@+,%0@+\n\t"
			       ".chip 68k\n\t"
			       "subqw  #8,%2\n\t"
			       "subqw  #8,%2\n\t"
			       "dbra   %1,1b\n\t"
			       : "=a" (sp), "=d" (tmp)
			       : "a" (s), "0" (sp), "1" ((count - 16) / 16 - 1)
			       );
  }

#else
  __asm__ __volatile__("1:\t"
		       "movel %2,%0@+\n\t"
		       "movel %2,%0@+\n\t"
		       "movel %2,%0@+\n\t"
		       "movel %2,%0@+\n\t"
		       "movel %2,%0@+\n\t"
		       "movel %2,%0@+\n\t"
		       "movel %2,%0@+\n\t"
		       "movel %2,%0@+\n\t"
		       "dbra  %1,1b\n\t"
		       : "=a" (s), "=d" (tmp)
		       : "d" (data), "0" (s), "1" (count / 32 - 1)
		       );
#endif

  return xs;
}

extern void *memset(void *,int,__kernel_size_t);

#define __memset_const(s,c,count) \
((count==PAGE_SIZE) ? \
  __memset_page((s),(c),(count)) : \
  __memset_g((s),(c),(count)))

#define memset(s, c, count) \
(__builtin_constant_p(count) ? \
 __memset_const((s),(c),(count)) : \
 __memset_g((s),(c),(count)))

#define __HAVE_ARCH_MEMCPY
extern void * memcpy(void *, const void *, size_t );
/*
 * __builtin_memcpy() does not handle page-sized memcpys very well,
 * thus following the same assumptions as for page-sized memsets, this
 * function copies page-sized areas using an unrolled loop, without
 * considering alignment.
 *
 * For the 680[46]0 only kernels we use the move16 instruction instead
 * as it writes through the data-cache, invalidating the cache-lines
 * touched. In this way we do not use up the entire data-cache (well,
 * half of it on the 68060) by copying a page. An unrolled loop of two
 * move16 instructions seem to the fastest. The only caveat is that
 * both source and destination must be 16-byte aligned, if not we fall
 * back to the generic memcpy function.  - Jes
 */
static inline void * __memcpy_page(void * to, const void * from, size_t count)
{
  unsigned long tmp;
  void *xto = to;

#ifdef CPU_M68040_OR_M68060_ONLY

  if (((unsigned long) to | (unsigned long) from) & 0x0f)
	  return memcpy(to, from, count);

  __asm__ __volatile__("1:\t"
		       ".chip 68040\n\t"
		       "move16 %1@+,%0@+\n\t"
		       "move16 %1@+,%0@+\n\t"
		       ".chip 68k\n\t"
		       "dbra  %2,1b\n\t"
		       : "=a" (to), "=a" (from), "=d" (tmp)
		       : "0" (to), "1" (from) , "2" (count / 32 - 1)
		       );
#else
  __asm__ __volatile__("1:\t"
		       "movel %1@+,%0@+\n\t"
		       "movel %1@+,%0@+\n\t"
		       "movel %1@+,%0@+\n\t"
		       "movel %1@+,%0@+\n\t"
		       "movel %1@+,%0@+\n\t"
		       "movel %1@+,%0@+\n\t"
		       "movel %1@+,%0@+\n\t"
		       "movel %1@+,%0@+\n\t"
		       "dbra  %2,1b\n\t"
		       : "=a" (to), "=a" (from), "=d" (tmp)
		       : "0" (to), "1" (from) , "2" (count / 32 - 1)
		       );
#endif
  return xto;
}

#define __memcpy_const(to, from, n) \
((n==PAGE_SIZE) ? \
  __memcpy_page((to),(from),(n)) : \
  __builtin_memcpy((to),(from),(n)))

#define memcpy(to, from, n) \
(__builtin_constant_p(n) ? \
 __memcpy_const((to),(from),(n)) : \
 memcpy((to),(from),(n)))

#define __HAVE_ARCH_MEMMOVE
static inline void * memmove(void * dest,const void * src, size_t n)
{
  void *xdest = dest;
  size_t temp;

  if (!n)
    return xdest;

  if (dest < src)
    {
      if ((long) dest & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *cdest++ = *csrc++;
	  dest = cdest;
	  src = csrc;
	  n--;
	}
      if (n > 2 && (long) dest & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *sdest++ = *ssrc++;
	  dest = sdest;
	  src = ssrc;
	  n -= 2;
	}
      temp = n >> 2;
      if (temp)
	{
	  long *ldest = dest;
	  const long *lsrc = src;
	  temp--;
	  do
	    *ldest++ = *lsrc++;
	  while (temp--);
	  dest = ldest;
	  src = lsrc;
	}
      if (n & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *sdest++ = *ssrc++;
	  dest = sdest;
	  src = ssrc;
	}
      if (n & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *cdest = *csrc;
	}
    }
  else
    {
      dest = (char *) dest + n;
      src = (const char *) src + n;
      if ((long) dest & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *--cdest = *--csrc;
	  dest = cdest;
	  src = csrc;
	  n--;
	}
      if (n > 2 && (long) dest & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *--sdest = *--ssrc;
	  dest = sdest;
	  src = ssrc;
	  n -= 2;
	}
      temp = n >> 2;
      if (temp)
	{
	  long *ldest = dest;
	  const long *lsrc = src;
	  temp--;
	  do
	    *--ldest = *--lsrc;
	  while (temp--);
	  dest = ldest;
	  src = lsrc;
	}
      if (n & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *--sdest = *--ssrc;
	  dest = sdest;
	  src = ssrc;
	}
      if (n & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *--cdest = *--csrc;
	}
    }
  return xdest;
}

#define __HAVE_ARCH_MEMCMP
extern int memcmp(const void * ,const void * ,size_t );
#define memcmp(cs, ct, n) \
(__builtin_constant_p(n) ? \
 __builtin_memcmp((cs),(ct),(n)) : \
 memcmp((cs),(ct),(n)))

#define __HAVE_ARCH_MEMCHR
static inline void *memchr(const void *cs, int c, size_t count)
{
	/* Someone else can optimize this, I don't care - tonym@mac.linux-m68k.org */
	unsigned char *ret = (unsigned char *)cs;
	for(;count>0;count--,ret++)
		if(*ret == c) return ret;

	return NULL;
}

#endif /* _M68K_STRING_H_ */
