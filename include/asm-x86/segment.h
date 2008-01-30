#ifndef _ASM_X86_SEGMENT_H_
#define _ASM_X86_SEGMENT_H_

#ifdef CONFIG_X86_32
# include "segment_32.h"
#else
# include "segment_64.h"
#endif

#ifndef CONFIG_PARAVIRT
#define get_kernel_rpl()  0
#endif

#endif
