#ifndef __SOCKMAP_HELPERS__
#define __SOCKMAP_HELPERS__

#include "socket_helpers.h"

#define MAX_TEST_NAME 80

#define __always_unused	__attribute__((__unused__))

#define xbpf_map_delete_elem(fd, key)                                          \
	({                                                                     \
		int __ret = bpf_map_delete_elem((fd), (key));                  \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_delete");                              \
		__ret;                                                         \
	})

#define xbpf_map_lookup_elem(fd, key, val)                                     \
	({                                                                     \
		int __ret = bpf_map_lookup_elem((fd), (key), (val));           \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_lookup");                              \
		__ret;                                                         \
	})

#define xbpf_map_update_elem(fd, key, val, flags)                              \
	({                                                                     \
		int __ret = bpf_map_update_elem((fd), (key), (val), (flags));  \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_update");                              \
		__ret;                                                         \
	})

#define xbpf_prog_attach(prog, target, type, flags)                            \
	({                                                                     \
		int __ret =                                                    \
			bpf_prog_attach((prog), (target), (type), (flags));    \
		if (__ret < 0)                                               \
			FAIL_ERRNO("prog_attach(" #type ")");                  \
		__ret;                                                         \
	})

#define xbpf_prog_detach2(prog, target, type)                                  \
	({                                                                     \
		int __ret = bpf_prog_detach2((prog), (target), (type));        \
		if (__ret < 0)                                               \
			FAIL_ERRNO("prog_detach2(" #type ")");                 \
		__ret;                                                         \
	})

#define xpthread_create(thread, attr, func, arg)                               \
	({                                                                     \
		int __ret = pthread_create((thread), (attr), (func), (arg));   \
		errno = __ret;                                                 \
		if (__ret)                                                     \
			FAIL_ERRNO("pthread_create");                          \
		__ret;                                                         \
	})

#define xpthread_join(thread, retval)                                          \
	({                                                                     \
		int __ret = pthread_join((thread), (retval));                  \
		errno = __ret;                                                 \
		if (__ret)                                                     \
			FAIL_ERRNO("pthread_join");                            \
		__ret;                                                         \
	})

static inline int add_to_sockmap(int sock_mapfd, int fd1, int fd2)
{
	u64 value;
	u32 key;
	int err;

	key = 0;
	value = fd1;
	err = xbpf_map_update_elem(sock_mapfd, &key, &value, BPF_NOEXIST);
	if (err)
		return err;

	key = 1;
	value = fd2;
	return xbpf_map_update_elem(sock_mapfd, &key, &value, BPF_NOEXIST);
}

#endif // __SOCKMAP_HELPERS__
