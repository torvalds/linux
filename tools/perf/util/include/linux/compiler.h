#ifndef _PERF_LINUX_COMPILER_H_
#define _PERF_LINUX_COMPILER_H_

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

#endif
