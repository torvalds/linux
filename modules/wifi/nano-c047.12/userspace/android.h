/* Copyright (C) 2009 Nanoradio AB */

/* glibc stuff which is missing from bionic library.
 * Should be included when building nanoradio applications under Android.
 */

#ifndef __android_h__
#define __android_h__

#ifdef ANDROID

#include <stdio.h>

void warn(const char *fmt, ...);
void err(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...);

/* Emulate sys/queue.h */
#define TAILQ_EMPTY(head)               ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head)               ((head)->tqh_first)
#define TAILQ_NEXT(elm, field)          ((elm)->field.tqe_next)

#define TAILQ_FOREACH(var, head, field)         \
	for ((var) = TAILQ_FIRST((head));       \
             (var) != NULL;                     \
             (var) = TAILQ_NEXT((var), field))

#define TAILQ_HEAD(name, type)                      \
	struct name {                               	\
		struct type *tqh_first; /* first element */ \
		struct type **tqh_last; /* addr of last next element */ \
	}

#define TAILQ_ENTRY(type)    						\
	struct {                                		\
		struct type *tqe_next;  /* next element */	\
		struct type **tqe_prev; /* address of previous next element */  \
	}

#define TAILQ_INSERT_TAIL(head, elm, field) do {			\
	(elm)->field.tqe_next = NULL;					\
	(elm)->field.tqe_prev = (head)->tqh_last;			\
	*(head)->tqh_last = (elm);					\
	(head)->tqh_last = &(elm)->field.tqe_next;			\
} while (0)

#define TAILQ_REMOVE(head, elm, field) do {				\
	if (((elm)->field.tqe_next) != NULL)				\
		(elm)->field.tqe_next->field.tqe_prev = 		\
		    (elm)->field.tqe_prev;				\
	else								\
		(head)->tqh_last = (elm)->field.tqe_prev;		\
	*(elm)->field.tqe_prev = (elm)->field.tqe_next;			\
} while (0)

#define	TAILQ_INIT(head) do {						\
	(head)->tqh_first = NULL;					\
	(head)->tqh_last = &(head)->tqh_first;				\
} while (0)

#endif /* ANDROID */
#endif /* __android_h__ */
