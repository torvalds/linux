/* Public domain. */

#ifndef _LINUX_CONTAINER_OF_H
#define _LINUX_CONTAINER_OF_H

#define container_of(ptr, type, member) ({			\
	const __typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#define container_of_const(p, t, m)	container_of(p, t, m)

#define typeof_member(s, e)	typeof(((s *)0)->e)

#endif
