#ifndef _LINUX_PERSONALITY_H
#define _LINUX_PERSONALITY_H

#include <uapi/linux/personality.h>


/*
 * Handling of different ABIs (personalities).
 */

struct exec_domain;
struct pt_regs;

extern int		register_exec_domain(struct exec_domain *);
extern int		unregister_exec_domain(struct exec_domain *);
extern int		__set_personality(unsigned int);


/*
 * Description of an execution domain.
 * 
 * The first two members are refernced from assembly source
 * and should stay where they are unless explicitly needed.
 */
typedef void (*handler_t)(int, struct pt_regs *);

struct exec_domain {
	const char		*name;		/* name of the execdomain */
	handler_t		handler;	/* handler for syscalls */
	unsigned char		pers_low;	/* lowest personality */
	unsigned char		pers_high;	/* highest personality */
	unsigned long		*signal_map;	/* signal mapping */
	unsigned long		*signal_invmap;	/* reverse signal mapping */
	struct map_segment	*err_map;	/* error mapping */
	struct map_segment	*socktype_map;	/* socket type mapping */
	struct map_segment	*sockopt_map;	/* socket option mapping */
	struct map_segment	*af_map;	/* address family mapping */
	struct module		*module;	/* module context of the ed. */
	struct exec_domain	*next;		/* linked list (internal) */
};

/*
 * Return the base personality without flags.
 */
#define personality(pers)	(pers & PER_MASK)


/*
 * Change personality of the currently running process.
 */
#define set_personality(pers) \
	((current->personality == (pers)) ? 0 : __set_personality(pers))

#endif /* _LINUX_PERSONALITY_H */
