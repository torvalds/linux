/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RCUPDATE_H
#define _RCUPDATE_H

#include <urcu.h>

#define rcu_dereference_raw(p) rcu_dereference(p)
#define rcu_dereference_protected(p, cond) rcu_dereference(p)

#endif
