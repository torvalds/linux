#ifndef __TRIGGER_H_
#define __TRIGGER_H_ 1

#include "util/debug.h"
#include "asm/bug.h"

/*
 * Use trigger to model operations which need to be executed when
 * an event (a signal, for example) is observed.
 *
 * States and transits:
 *
 *
 *  OFF--(on)--> READY --(hit)--> HIT
 *                 ^               |
 *                 |            (ready)
 *                 |               |
 *                  \_____________/
 *
 * is_hit and is_ready are two key functions to query the state of
 * a trigger. is_hit means the event already happen; is_ready means the
 * trigger is waiting for the event.
 */

struct trigger {
	volatile enum {
		TRIGGER_ERROR		= -2,
		TRIGGER_OFF		= -1,
		TRIGGER_READY		= 0,
		TRIGGER_HIT		= 1,
	} state;
	const char *name;
};

#define TRIGGER_WARN_ONCE(t, exp) \
	WARN_ONCE(t->state != exp, "trigger '%s' state transist error: %d in %s()\n", \
		  t->name, t->state, __func__)

static inline bool trigger_is_available(struct trigger *t)
{
	return t->state >= 0;
}

static inline bool trigger_is_error(struct trigger *t)
{
	return t->state <= TRIGGER_ERROR;
}

static inline void trigger_on(struct trigger *t)
{
	TRIGGER_WARN_ONCE(t, TRIGGER_OFF);
	t->state = TRIGGER_READY;
}

static inline void trigger_ready(struct trigger *t)
{
	if (!trigger_is_available(t))
		return;
	t->state = TRIGGER_READY;
}

static inline void trigger_hit(struct trigger *t)
{
	if (!trigger_is_available(t))
		return;
	TRIGGER_WARN_ONCE(t, TRIGGER_READY);
	t->state = TRIGGER_HIT;
}

static inline void trigger_off(struct trigger *t)
{
	if (!trigger_is_available(t))
		return;
	t->state = TRIGGER_OFF;
}

static inline void trigger_error(struct trigger *t)
{
	t->state = TRIGGER_ERROR;
}

static inline bool trigger_is_ready(struct trigger *t)
{
	return t->state == TRIGGER_READY;
}

static inline bool trigger_is_hit(struct trigger *t)
{
	return t->state == TRIGGER_HIT;
}

#define DEFINE_TRIGGER(n) \
struct trigger n = {.state = TRIGGER_OFF, .name = #n}
#endif
