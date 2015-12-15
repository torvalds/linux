#ifndef __CLK_MMP_H
#define __CLK_MMP_H

#include <linux/types.h>

extern void pxa168_clk_init(phys_addr_t mpmu_phys,
			    phys_addr_t apmu_phys,
			    phys_addr_t apbc_phys);
extern void pxa910_clk_init(phys_addr_t mpmu_phys,
			    phys_addr_t apmu_phys,
			    phys_addr_t apbc_phys,
			    phys_addr_t apbcp_phys);
extern void mmp2_clk_init(phys_addr_t mpmu_phys,
			  phys_addr_t apmu_phys,
			  phys_addr_t apbc_phys);

#endif
