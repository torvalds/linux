/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ENUMERATED_REF_TYPES_H
#define _BCACHEFS_ENUMERATED_REF_TYPES_H

#include <linux/percpu-refcount.h>

struct enumerated_ref {
#ifdef ENUMERATED_REF_DEBUG
	unsigned		nr;
	bool			dying;
	atomic_long_t		*refs;
#else
	struct percpu_ref	ref;
#endif
	void			(*stop_fn)(struct enumerated_ref *);
	struct completion	stop_complete;
};

#endif /* _BCACHEFS_ENUMERATED_REF_TYPES_H */
