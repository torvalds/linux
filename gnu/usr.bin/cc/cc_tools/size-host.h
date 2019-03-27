/* $FreeBSD$ */

#ifdef	SIZEOF_INT
# undef	SIZEOF_INT
#endif

#ifdef	SIZEOF_SHORT
# undef	SIZEOF_SHORT
#endif

#ifdef	SIZEOF_LONG
# undef	SIZEOF_LONG
#endif

#ifdef	SIZEOF_VOID_P
# undef	SIZEOF_VOID_P
#endif

#ifdef	SIZEOF_LONG_LONG
# undef	SIZEOF_LONG_LONG
#endif

#ifdef  HOST_WIDE_INT
# undef	HOST_WIDE_INT
#endif

#define SIZEOF_INT		4
#define SIZEOF_SHORT		2
#define	SIZEOF_LONG_LONG	8

#if __LP64__
#define	SIZEOF_LONG		8
#define	SIZEOF_VOID_P		8
#define	HOST_WIDE_INT		long
#else
#define	SIZEOF_LONG		4
#define	SIZEOF_VOID_P		4
#define	HOST_WIDE_INT		long long
#endif

#ifdef WORDS_BIGENDIAN
#undef WORDS_BIGENDIAN
#endif

#if defined(__sparc64__) || defined(__ARMEB__)
#define WORDS_BIGENDIAN		1
#endif
