/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TIME_NAMESPACE_INTERNAL_H
#define _TIME_NAMESPACE_INTERNAL_H

#include <linux/mutex.h>

/*
 * Protects possibly multiple offsets writers racing each other
 * and tasks entering the namespace.
 */
extern struct mutex timens_offset_lock;

#endif /* _TIME_NAMESPACE_INTERNAL_H */
