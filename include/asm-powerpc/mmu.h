#ifndef _ASM_POWERPC_MMU_H_
#define _ASM_POWERPC_MMU_H_
#ifdef __KERNEL__

#ifdef CONFIG_PPC64
/* 64-bit classic hash table MMU */
#  include <asm/mmu-hash64.h>
#elif defined(CONFIG_PPC_STD_MMU)
/* 32-bit classic hash table MMU */
#  include <asm/mmu-hash32.h>
#elif defined(CONFIG_44x)
/* 44x-style software loaded TLB */
#  include <asm/mmu-44x.h>
#elif defined(CONFIG_FSL_BOOKE)
/* Freescale Book-E software loaded TLB */
#  include <asm/mmu-fsl-booke.h>
#elif defined (CONFIG_PPC_8xx)
/* Motorola/Freescale 8xx software loaded TLB */
#  include <asm/mmu-8xx.h>
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_MMU_H_ */
