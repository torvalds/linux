/*
 * useful structures/macros
 *
 * Operating Systems 2
 */

#ifndef UTIL_H_
#define UTIL_H_		1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)

#include <windows.h>

static VOID PrintLastError(const PCHAR message)
{
	CHAR errBuff[1024];

	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL,
		GetLastError(),
		0,
		errBuff,
		sizeof(errBuff) - 1,
		NULL);

	fprintf(stderr, "%s: %s\n", message, errBuff);
}

#define ERR(call_description)					\
	do {							\
		fprintf(stderr, "(%s, %d): ",			\
				__FILE__, __LINE__);		\
			PrintLastError(call_description);	\
	} while (0)

#elif defined(__linux__)

/* error printing macro */
#define ERR(call_description)				\
	do {						\
		fprintf(stderr, "(%s, %d): ",		\
				__FILE__, __LINE__);	\
			perror(call_description);	\
	} while (0)

#else
  #error "Unknown platform"
#endif

/* print error (call ERR) and exit */
#define DIE(assertion, call_description)		\
	do {						\
		if (assertion) {			\
			ERR(call_description);		\
			exit(EXIT_FAILURE);		\
		}					\
	} while (0)

#ifdef __cplusplus
}
#endif

#endif
