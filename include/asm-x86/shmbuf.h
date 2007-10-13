#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "shmbuf_32.h"
# else
#  include "shmbuf_64.h"
# endif
#else
# ifdef __i386__
#  include "shmbuf_32.h"
# else
#  include "shmbuf_64.h"
# endif
#endif
