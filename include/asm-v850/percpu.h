#ifndef __V850_PERCPU_H__
#define __V850_PERCPU_H__

#include <asm-generic/percpu.h>

/* This is a stupid hack to satisfy some grotty implicit include-file
   dependency; basically, <linux/smp.h> uses BUG_ON, which calls BUG, but
   doesn't include the necessary headers to define it.  In the twisted
   festering mess of includes this must all be resolved somehow on other
   platforms, but I haven't the faintest idea how, and don't care; here will
   do, even though doesn't actually make any sense.  */
#include <asm/page.h>

#endif /* __V850_PERCPU_H__ */
