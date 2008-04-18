#ifdef __KERNEL__
# if defined(CONFIG_X86_32) || defined(__i386__)
#  include "unistd_32.h"
# else
#  include "unistd_64.h"
# endif
#endif
