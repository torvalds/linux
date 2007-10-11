#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "msgbuf_32.h"
# else
#  include "msgbuf_64.h"
# endif
#else
# ifdef __i386__
#  include "msgbuf_32.h"
# else
#  include "msgbuf_64.h"
# endif
#endif
