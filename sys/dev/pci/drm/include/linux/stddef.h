/* Public domain. */

#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#define DECLARE_FLEX_ARRAY(t, n) \
	struct { struct{} n ## __unused; t n[]; }

#define offsetofend(s, e) (offsetof(s, e) + sizeof((((s *)0)->e)))

#endif
