/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_IOASID_H
#define __LINUX_IOASID_H

#include <linux/types.h>
#include <linux/errno.h>

#define INVALID_IOASID ((ioasid_t)-1)
typedef unsigned int ioasid_t;

struct ioasid_set {
	int dummy;
};

#define DECLARE_IOASID_SET(name) struct ioasid_set name = { 0 }

#if IS_ENABLED(CONFIG_IOASID)
ioasid_t ioasid_alloc(struct ioasid_set *set, ioasid_t min, ioasid_t max,
		      void *private);
void ioasid_free(ioasid_t ioasid);
void *ioasid_find(struct ioasid_set *set, ioasid_t ioasid,
		  bool (*getter)(void *));
int ioasid_set_data(ioasid_t ioasid, void *data);

#else /* !CONFIG_IOASID */
static inline ioasid_t ioasid_alloc(struct ioasid_set *set, ioasid_t min,
				    ioasid_t max, void *private)
{
	return INVALID_IOASID;
}

static inline void ioasid_free(ioasid_t ioasid)
{
}

static inline void *ioasid_find(struct ioasid_set *set, ioasid_t ioasid,
				bool (*getter)(void *))
{
	return NULL;
}

static inline int ioasid_set_data(ioasid_t ioasid, void *data)
{
	return -ENOTSUPP;
}

#endif /* CONFIG_IOASID */
#endif /* __LINUX_IOASID_H */
