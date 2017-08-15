#ifndef _LINUX_ERRSEQ_H
#define _LINUX_ERRSEQ_H

/* See lib/errseq.c for more info */

typedef u32	errseq_t;

errseq_t __errseq_set(errseq_t *eseq, int err);
static inline void errseq_set(errseq_t *eseq, int err)
{
	/* Optimize for the common case of no error */
	if (unlikely(err))
		__errseq_set(eseq, err);
}

errseq_t errseq_sample(errseq_t *eseq);
int errseq_check(errseq_t *eseq, errseq_t since);
int errseq_check_and_advance(errseq_t *eseq, errseq_t *since);
#endif
