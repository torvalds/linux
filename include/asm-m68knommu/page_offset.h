
#include <linux/config.h>

/* This handles the memory map.. */

#ifdef CONFIG_COLDFIRE
#if defined(CONFIG_SMALL)
#define PAGE_OFFSET_RAW		0x30020000
#elif defined(CONFIG_CFV240)
#define PAGE_OFFSET_RAW		0x02000000
#else
#define PAGE_OFFSET_RAW		0x00000000
#endif
#endif

#ifdef CONFIG_M68360
#define PAGE_OFFSET_RAW     0x00000000
#endif

#ifdef CONFIG_PILOT
#ifdef CONFIG_M68328
#define PAGE_OFFSET_RAW		0x10000000
#endif
#ifdef CONFIG_M68EZ328
#define PAGE_OFFSET_RAW		0x00000000
#endif
#endif
#ifdef CONFIG_UCSIMM
#define PAGE_OFFSET_RAW		0x00000000
#endif

#if defined(CONFIG_UCDIMM) || defined(CONFIG_DRAGEN2)
#ifdef CONFIG_M68VZ328 
#define PAGE_OFFSET_RAW		0x00000000
#endif /* CONFIG_M68VZ328 */
#endif /* CONFIG_UCDIMM */

#ifdef CONFIG_M68EZ328ADS
#define PAGE_OFFSET_RAW		0x00000000
#endif
#ifdef CONFIG_ALMA_ANS
#define PAGE_OFFSET_RAW		0x00000000
#endif
#ifdef CONFIG_M68EN302
#define PAGE_OFFSET_RAW		0x00000000
#endif

