#ifndef _X86_64_PAGE_H
#define _X86_64_PAGE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#endif /* !__ASSEMBLY__ */

#ifndef __ASSEMBLY__


#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)          ((pfn) < end_pfn)
#endif


#endif /* _X86_64_PAGE_H */
