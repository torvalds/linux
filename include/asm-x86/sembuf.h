#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "sembuf_32.h"
# else
#  include "sembuf_64.h"
# endif
#else
# ifdef __i386__
#  include "sembuf_32.h"
# else
#  include "sembuf_64.h"
# endif
#endif
