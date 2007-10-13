#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "a.out_32.h"
# else
#  include "a.out_64.h"
# endif
#else
# ifdef __i386__
#  include "a.out_32.h"
# else
#  include "a.out_64.h"
# endif
#endif
