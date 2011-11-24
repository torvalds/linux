/* $Id: nanoutil.h 18838 2011-04-26 11:37:17Z johe $ */
#ifndef _NANOUTIL_H
#define _NANOUTIL_H

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sysctl.h>
#include <linux/string.h>
#include <linux/sched.h>

#define PRINTK_LEVEL KERN_INFO
void nano_util_printbuf(const void*, size_t, const char*);

#ifdef WIFI_DEBUG_ON

extern unsigned int nrx_debug;

#define DEBUG_ERROR	(1 << 0)
#define DEBUG_TRACE	(1 << 1)
#define DEBUG_MISC	(1 << 2)
#define DEBUG_PRINTBUF	(1 << 3)
#define DEBUG_TRANSPORT	(1 << 4)
#define DEBUG_CALL	(1 << 6)
#define DEBUG_MEMORY	(1 << 7)

/* for gcc 2.95, the space before the comma just 
   before ##__VA_ARGS__ is important */
#define KDEBUGU(F, ...) ({                  \
   printk(PRINTK_LEVEL "%s:%d:[%d]: " F,    \
	  __func__, __LINE__,                   \
	  current->pid , ##__VA_ARGS__);        \
})

#define KDEBUG(L, F, ...) ({ 			\
   if(nrx_debug & (DEBUG_ ## L)) 		\
      KDEBUGU(F "\n" , ##__VA_ARGS__);		\
})

#define KDEBUGX(L, X, ...) ({ 			\
   if(nrx_debug & (DEBUG_ ## L)) 		\
      KDEBUGU(X , ##__VA_ARGS__);		\
})

#define KDEBUG_DO(L, X) ({ 			\
   if(nrx_debug & (DEBUG_ ## L)) 		\
      X;		\
})

#define KDEBUG_BUF(L, B, S, T) ({			\
   if(nrx_debug & (DEBUG_ ## L)) 			\
      nano_util_printbuf((B), (S), (T));	\
})

#else /* !WIFI_DEBUG_ON */

#define KDEBUGU(X, ...)        {}
#define KDEBUG(L, X, ...)      {}
#define KDEBUGX(L, X, ...)     {}
#define KDEBUG_DO(L, X)        {}
#define KDEBUG_BUF(L, B, S, T) {}

#endif /* WIFI_DEBUG_ON */

#define ASSERT(EXPR) do { if(!(EXPR)) { printk(KERN_ERR "%s:%s:%d: assert failed:%s", __FILE__, __func__, __LINE__, #EXPR); BUG(); } } while(0);

int nano_util_register_sysctl(struct ctl_table*);
int nano_util_unregister_sysctl(ctl_table*);

#undef SYSCTLENTRY
#define SYSCTLENTRY_MODE(N, D, H, M)            \
   SYSCTL_CTL_NAME(CTL_ANY)                     \
   .procname = #N,                              \
   .data = &(D),                                \
   .maxlen = sizeof(D),                         \
   .mode = (M),                                 \
   .proc_handler = (H)
#define SYSCTLEND                               \
   SYSCTL_CTL_NAME(CTL_NONE)                    \
   .procname = NULL

#define SYSCTLENTRY(N, D, H) SYSCTLENTRY_MODE(N, (D), (H), 0600)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
#  define SYSCTL_CTL_NAME(N) /* empty */
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33) */
#  define SYSCTL_CTL_NAME(N) .ctl_name = (N),
#  if defined(FORCE_SYSCTL_SYSCALL_CHECK) && (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 24))
#    define CONFIG_SYSCTL_SYSCALL_CHECK /* workaround for freescale 2.6.24 */
#  endif
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 21)
#    define CTL_ANY -1 /* this should be seen as a temporary workaround */
#  endif
#  if defined(CONFIG_SYSCTL_SYSCALL_CHECK) && defined(CTL_UNNUMBERED)
#  else
#    define HAVE_STATIC_SYSCTL_NUMBERING 1
#  endif
#endif /* LINUX_VERSION_CODE */

void nano_util_init(void);
void nano_util_cleanup(void);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,70)
/* strl{cpy,cat} implementation from libroken */
static inline size_t
nano_strlcpy(char *dst, const char *src, size_t dst_sz)
{
   size_t n;

   for (n = 0; n < dst_sz; n++) {
      if ((*dst++ = *src++) == '\0')
         break;
   }

   if (n < dst_sz)
      return n;
   if (n > 0)
      *(dst - 1) = '\0';
   return n + strlen (src);
}

static inline size_t
nano_strlcat (char *dst, const char *src, size_t dst_sz)
{
   size_t len = strlen(dst);

   //BUG_ON(len >= dst_sz);

   return len + nano_strlcpy (dst + len, src, dst_sz - len);
}

#define strlcpy(D, S, L) nano_strlcpy((D), (S), (L))
#define strlcat(D, S, L) nano_strlcat((D), (S), (L))
#endif /* linux < 2.5.70 */

#endif /* ! _NANOUTIL_H */
