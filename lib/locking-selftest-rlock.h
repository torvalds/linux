/* SPDX-License-Identifier: GPL-2.0 */
#undef LOCK
#define LOCK		RL

#undef UNLOCK
#define UNLOCK		RU

#undef RLOCK
#define RLOCK		RL

#undef WLOCK
#define WLOCK		WL

#undef INIT
#define INIT		RWI
