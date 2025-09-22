
#ifndef _SRP_COMPAT_H_
#define _SRP_COMPAT_H_

#include <sys/srp.h>
#include <sys/queue.h>

/*
 * SRP glue.
 */

#define srp_follow(_sr, _s)		((_s)->ref)
#define srp_leave(_sr)			do { } while (0)
#define srp_swap(_srp, _v)		srp_swap_locked((_srp), (_v))
#define srp_update(_gc, _srp, _v)	srp_update_locked((_gc), (_srp), (_v))
#define srp_finalize(_v, _wchan)	((void)0)

#define srp_get_locked(_s)		((_s)->ref)

static inline void *
srp_enter(struct srp_ref *_sr, struct srp *_s)
{
	return (_s->ref);
}

static inline void *
srp_swap_locked(struct srp *srp, void *nv)
{
	void *ov;

	ov = srp->ref;
	srp->ref = nv;

	return (ov);
}

#define srp_update_locked(_gc, _s, _v) do {				\
	void *ov;							\
									\
	ov = srp_swap_locked(_s, _v);					\
									\
	if (ov != NULL)							\
		((_gc)->srp_gc_dtor)((_gc)->srp_gc_cookie, ov);		\
} while (0)

/*
 * SRPL glue.
 */

#define SRPL_INIT(_sl)			SLIST_INIT(_sl)
#undef SRPL_HEAD
#define SRPL_HEAD(name, entry)		SLIST_HEAD(name, entry)
#undef SRPL_ENTRY
#define SRPL_ENTRY(type)		SLIST_ENTRY(type)

#define SRPL_FIRST(_sr, _sl)		SLIST_FIRST(_sl);
#define SRPL_NEXT(_sr, _e, _ENTRY)	SLIST_NEXT(_e, _ENTRY)
#define SRPL_FOLLOW(_sr, _e, _ENTRY)	SLIST_NEXT(_e, _ENTRY)
#define SRPL_LEAVE(_sr)			((void)_sr)

#define SRPL_FOREACH(_c, _srp, _sl, _ENTRY)				\
		SLIST_FOREACH(_c, _sl, _ENTRY)


#define SRPL_EMPTY_LOCKED(_sl)		SLIST_EMPTY(_sl)
#define SRPL_FIRST_LOCKED(_sl)		SLIST_FIRST(_sl)
#define SRPL_NEXT_LOCKED(_e, _ENTRY)	SLIST_NEXT(_e, _ENTRY)

#define SRPL_FOREACH_LOCKED(_c, _sl, _ENTRY)				\
		SLIST_FOREACH(_c, _sl, _ENTRY)

#define SRPL_FOREACH_SAFE_LOCKED(_c, _sl, _ENTRY, _tc)			\
		SLIST_FOREACH_SAFE(_c, _sl, _ENTRY, _tc)

#define SRPL_INSERT_HEAD_LOCKED(_rc, _sl, _e, _ENTRY)			\
	do {								\
		(_rc)->srpl_ref((_rc)->srpl_cookie, _e);		\
		SLIST_INSERT_HEAD(_sl, _e, _ENTRY);			\
	} while (0)

#define SRPL_INSERT_AFTER_LOCKED(_rc, _se, _e, _ENTRY)			\
	do {								\
		(_rc)->srpl_ref((_rc)->srpl_cookie, _e);		\
		SLIST_INSERT_AFTER(_se, _e, _ENTRY);			\
	} while (0)

#define SRPL_REMOVE_LOCKED(_rc, _sl, _e, _type, _ENTRY)			\
	do {								\
		SLIST_REMOVE(_sl, _e, _type, _ENTRY);			\
		((_rc)->srpl_gc.srp_gc_dtor)((_rc)->srpl_gc.srp_gc_cookie, _e);\
	} while (0)

#endif /* _SRP_COMPAT_H_ */
