#ifndef _TOOLS_LINUX_COMPILER_H_
#define _TOOLS_LINUX_COMPILER_H_

#ifndef __always_inline
# define __always_inline	inline __attribute__((always_inline))
#endif

#define __user

#ifndef __attribute_const__
# define __attribute_const__
#endif

#ifndef __maybe_unused
# define __maybe_unused		__attribute__((unused))
#endif

#ifndef __packed
# define __packed		__attribute__((__packed__))
#endif

#ifndef __force
# define __force
#endif

#ifndef __weak
# define __weak			__attribute__((weak))
#endif

#ifndef likely
# define likely(x)		__builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
# define unlikely(x)		__builtin_expect(!!(x), 0)
#endif

#endif /* _TOOLS_LINUX_COMPILER_H */
