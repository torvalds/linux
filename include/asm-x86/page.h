#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "page_32.h"
# else
#  include "page_64.h"
# endif
#else
# ifdef __i386__
#  include "page_32.h"
# else
#  include "page_64.h"
# endif
#endif
