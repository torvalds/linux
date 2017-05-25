#ifndef _LIBLOCKDEP_LINUX_COMPILER_H_
#define _LIBLOCKDEP_LINUX_COMPILER_H_

#define __used		__attribute__((__unused__))
#define unlikely
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, val) x=(val)
#define RCU_INIT_POINTER(p, v) p=(v)

#define __packed	__attribute__((packed))

#endif
