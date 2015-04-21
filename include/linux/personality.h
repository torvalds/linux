#ifndef _LINUX_PERSONALITY_H
#define _LINUX_PERSONALITY_H

#include <uapi/linux/personality.h>

/*
 * Return the base personality without flags.
 */
#define personality(pers)	(pers & PER_MASK)

/*
 * Change personality of the currently running process.
 */
#define set_personality(pers)	(current->personality = (pers))

#endif /* _LINUX_PERSONALITY_H */
