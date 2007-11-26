#ifndef __LINUX_PCOUNTER_H
#define __LINUX_PCOUNTER_H

struct pcounter {
#ifdef CONFIG_SMP
	void		(*add)(struct pcounter *self, int inc);
	int		(*getval)(const struct pcounter *self);
	int		*per_cpu_values;
#else
	int		val;
#endif
};

/*
 * Special macros to let pcounters use a fast version of {getvalue|add}
 * using a static percpu variable per pcounter instead of an allocated one,
 * saving one dereference.
 * This might be changed if/when dynamic percpu vars become fast.
 */
#ifdef CONFIG_SMP
#include <linux/cpumask.h>
#include <linux/percpu.h>

#define DEFINE_PCOUNTER(NAME)					\
static DEFINE_PER_CPU(int, NAME##_pcounter_values);		\
static void NAME##_pcounter_add(struct pcounter *self, int inc)	\
{								\
       __get_cpu_var(NAME##_pcounter_values) += inc;		\
}								\
								\
static int NAME##_pcounter_getval(const struct pcounter *self)	\
{								\
       int res = 0, cpu;					\
								\
       for_each_possible_cpu(cpu)				\
               res += per_cpu(NAME##_pcounter_values, cpu);	\
       return res;						\
}

#define PCOUNTER_MEMBER_INITIALIZER(NAME, MEMBER)		\
	MEMBER = {						\
		.add	= NAME##_pcounter_add,			\
		.getval = NAME##_pcounter_getval,		\
	}

extern void pcounter_def_add(struct pcounter *self, int inc);
extern int pcounter_def_getval(const struct pcounter *self);

static inline int pcounter_alloc(struct pcounter *self)
{
	int rc = 0;
	if (self->add == NULL) {
		self->per_cpu_values = alloc_percpu(int);
		if (self->per_cpu_values != NULL) {
			self->add    = pcounter_def_add;
			self->getval = pcounter_def_getval;
		} else
			rc = 1;
	}
	return rc;
}

static inline void pcounter_free(struct pcounter *self)
{
	if (self->per_cpu_values != NULL) {
		free_percpu(self->per_cpu_values);
		self->per_cpu_values = NULL;
		self->getval = NULL;
		self->add = NULL;
	}
}

static inline void pcounter_add(struct pcounter *self, int inc)
{
	self->add(self, inc);
}

static inline int pcounter_getval(const struct pcounter *self)
{
	return self->getval(self);
}

#else /* CONFIG_SMP */

static inline void pcounter_add(struct pcounter *self, int inc)
{
	self->val += inc;
}

static inline int pcounter_getval(const struct pcounter *self)
{
	return self->val;
}

#define DEFINE_PCOUNTER(NAME)
#define PCOUNTER_MEMBER_INITIALIZER(NAME, MEMBER)
#define pcounter_alloc(self) 0
#define pcounter_free(self)

#endif /* CONFIG_SMP */

#endif /* __LINUX_PCOUNTER_H */
