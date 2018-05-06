/*
 * debugging macros
 *    heavily inspired by previous work and Internet resources
 *
 * uses C99 variadic macros
 * uses non-standard usage of the token-paste operator (##) for
 *   removing the comma symbol (,) when not followed by a token
 * uses non-standard __FUNCTION__ macro (MSVC doesn't support __func__)
 * tested on gcc 4.4.5 and Visual Studio 2008 (9.0), compiler version 15.00
 *
 * Razvan Deaconescu, razvan.deaconescu@cs.pub.ro
 */

#ifndef DEBUG_H_
#define DEBUG_H_	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* log levels */
enum {
	LOG_EMERG = 1,
	LOG_ALERT,
	LOG_CRIT,
	LOG_ERR,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG
};

/*
 * initialize default loglevel (for dlog)
 * may be redefined in the including code
 */

#ifndef LOG_LEVEL
#define LOG_LEVEL	LOG_WARNING
#endif

/*
 * define DEBUG macro as a compiler option:
 *    -DDEBUG for GCC
 *    /DDEBUG for MSVC
 */

#if defined DEBUG
#define dprintf(format, ...)					\
	fprintf(stderr, " [%s(), %s:%u] " format,		\
			__FUNCTION__, __FILE__, __LINE__,	\
			##__VA_ARGS__)
#else
#define dprintf(format, ...)					\
	do {							\
	} while (0)
#endif

#if defined DEBUG
#define dlog(level, format, ...)				\
	do {							\
		if (level <= LOG_LEVEL)				\
			dprintf(format, ##__VA_ARGS__);		\
	} while (0)
#else
#define dlog(level, format, ...)				\
	do {							\
	} while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif
