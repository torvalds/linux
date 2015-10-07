#ifndef _LINUX_ONCE_H
#define _LINUX_ONCE_H

#include <linux/types.h>
#include <linux/jump_label.h>

bool __get_random_once(void *buf, int nbytes, bool *done,
		       struct static_key *once_key);

#define get_random_once(buf, nbytes)					\
	({								\
		bool ___ret = false;					\
		static bool ___done = false;				\
		static struct static_key ___once_key =			\
			STATIC_KEY_INIT_TRUE;				\
		if (static_key_true(&___once_key))			\
			___ret = __get_random_once((buf),		\
						   (nbytes),		\
						   &___done,		\
						   &___once_key);	\
		___ret;							\
	})

#endif /* _LINUX_ONCE_H */
