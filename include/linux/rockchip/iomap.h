#ifndef __MACH_ROCKCHIP_IOMAP_H
#define __MACH_ROCKCHIP_IOMAP_H

#ifndef __ASSEMBLY__
#include <asm/io.h>
#endif

#define RK_IO_ADDRESS(x)                IOMEM(0xFED00000 + x)

#define RK_CRU_VIRT                     RK_IO_ADDRESS(0x00000000)
#define RK_GRF_VIRT                     RK_IO_ADDRESS(0x00010000)
#define RK_SGRF_VIRT                    (RK_GRF_VIRT + 0x1000)
#define RK_PMU_VIRT                     RK_IO_ADDRESS(0x00020000)
#define RK_ROM_VIRT                     RK_IO_ADDRESS(0x00030000)
#define RK_EFUSE_VIRT                   RK_IO_ADDRESS(0x00040000)
#define RK_GPIO_VIRT(n)                 RK_IO_ADDRESS(0x00050000 + (n) * 0x1000)
#define RK_DEBUG_UART_VIRT              RK_IO_ADDRESS(0x00060000)
#define RK_CPU_AXI_BUS_VIRT             RK_IO_ADDRESS(0x00070000)
#define RK_TIMER_VIRT                   RK_IO_ADDRESS(0x00080000)
#define RK_GIC_VIRT                     RK_IO_ADDRESS(0x00090000)
#define RK_BOOTRAM_VIRT                 RK_IO_ADDRESS(0x000a0000)
#define RK_DDR_VIRT                     RK_IO_ADDRESS(0x000d0000)

#define RK3188_CRU_PHYS                 0x20000000
#define RK3188_CRU_SIZE                 SZ_4K
#define RK3188_GRF_PHYS                 0x20008000
#define RK3188_GRF_SIZE                 SZ_4K
#define RK3188_PMU_PHYS                 0x20004000
#define RK3188_PMU_SIZE                 SZ_4K
#define RK3188_ROM_PHYS                 0x10120000
#define RK3188_ROM_SIZE                 SZ_16K
#define RK3188_EFUSE_PHYS               0x20010000
#define RK3188_EFUSE_SIZE               SZ_4K
#define RK3188_GPIO0_PHYS               0x2000a000
#define RK3188_GPIO1_PHYS               0x2003c000
#define RK3188_GPIO2_PHYS               0x2003e000
#define RK3188_GPIO3_PHYS               0x20080000
#define RK3188_GPIO_SIZE                SZ_4K
#define RK3188_CPU_AXI_BUS_PHYS         0x10128000
#define RK3188_CPU_AXI_BUS_SIZE         SZ_32K
#define RK3188_TIMER0_PHYS              0x20038000
#define RK3188_TIMER3_PHYS              0x2000e000
#define RK3188_TIMER_SIZE               SZ_4K
#define RK3188_DDR_PCTL_PHYS            0x20020000
#define RK3188_DDR_PCTL_SIZE            SZ_4K
#define RK3188_DDR_PUBL_PHYS            0x20040000
#define RK3188_DDR_PUBL_SIZE            SZ_4K
#define RK3188_UART0_PHYS               0x10124000
#define RK3188_UART1_PHYS               0x10126000
#define RK3188_UART2_PHYS               0x20064000
#define RK3188_UART3_PHYS               0x20068000
#define RK3188_UART_SIZE                SZ_4K

#define RK3288_CRU_PHYS                 0xFF760000
#define RK3288_CRU_SIZE                 SZ_4K
#define RK3288_GRF_PHYS                 0xFF770000
#define RK3288_GRF_SIZE                 SZ_4K
#define RK3288_SGRF_PHYS                0xFF740000
#define RK3288_SGRF_SIZE                SZ_4K
#define RK3288_PMU_PHYS                 0xFF730000
#define RK3288_PMU_SIZE                 SZ_4K
#define RK3288_ROM_PHYS                 0xFFFD0000
#define RK3288_ROM_SIZE                 (SZ_16K + SZ_4K)
#define RK3288_EFUSE_PHYS               0xFFB40000
#define RK3288_EFUSE_SIZE               SZ_4K
#define RK3288_GPIO0_PHYS               0xFF750000
#define RK3288_GPIO1_PHYS               0xFF780000
#define RK3288_GPIO2_PHYS               0xFF790000
#define RK3288_GPIO3_PHYS               0xFF7A0000
#define RK3288_GPIO4_PHYS               0xFF7B0000
#define RK3288_GPIO5_PHYS               0xFF7C0000
#define RK3288_GPIO6_PHYS               0xFF7D0000
#define RK3288_GPIO7_PHYS               0xFF7E0000
#define RK3288_GPIO8_PHYS               0xFF7F0000
#define RK3288_GPIO_SIZE                SZ_4K
#define RK3288_SERVICE_CORE_PHYS        0XFFA80000
#define RK3288_SERVICE_CORE_SIZE        SZ_4K
#define RK3288_SERVICE_DMAC_PHYS        0XFFA90000
#define RK3288_SERVICE_DMAC_SIZE        SZ_4K
#define RK3288_SERVICE_GPU_PHYS         0XFFAA0000
#define RK3288_SERVICE_GPU_SIZE         SZ_4K
#define RK3288_SERVICE_PERI_PHYS        0XFFAB0000
#define RK3288_SERVICE_PERI_SIZE        SZ_4K
#define RK3288_SERVICE_BUS_PHYS         0XFFAC0000
#define RK3288_SERVICE_BUS_SIZE         SZ_16K
#define RK3288_SERVICE_VIO_PHYS         0XFFAD0000
#define RK3288_SERVICE_VIO_SIZE         SZ_4K
#define RK3288_SERVICE_VIDEO_PHYS       0XFFAE0000
#define RK3288_SERVICE_VIDEO_SIZE       SZ_4K
#define RK3288_SERVICE_HEVC_PHYS        0XFFAF0000
#define RK3288_SERVICE_HEVC_SIZE        SZ_4K
#define RK3288_TIMER0_PHYS              0xFF6B0000
#define RK3288_TIMER6_PHYS              0xFF810000
#define RK3288_TIMER_SIZE               SZ_4K
#define RK3288_DDR_PCTL0_PHYS           0xFF610000
#define RK3288_DDR_PCTL1_PHYS           0xFF630000
#define RK3288_DDR_PCTL_SIZE            SZ_4K
#define RK3288_DDR_PUBL0_PHYS           0xFF620000
#define RK3288_DDR_PUBL1_PHYS           0xFF640000
#define RK3288_DDR_PUBL_SIZE            SZ_4K
#define RK3288_UART_BT_PHYS             0xFF180000
#define RK3288_UART_BB_PHYS             0xFF190000
#define RK3288_UART_DBG_PHYS            0xFF690000
#define RK3288_UART_GPS_PHYS            0xFF1B0000
#define RK3288_UART_EXP_PHYS            0xFF1C0000
#define RK3288_UART_SIZE                SZ_4K
#define RK3288_GIC_DIST_PHYS            0xFFC01000
#define RK3288_GIC_DIST_SIZE            SZ_4K
#define RK3288_GIC_CPU_PHYS             0xFFC02000
#define RK3288_GIC_CPU_SIZE             SZ_4K
#define RK3288_BOOTRAM_PHYS             0xFF720000
#define RK3288_BOOTRAM_SIZE             SZ_4K
#define RK3288_IMEM_PHYS                0xFF700000
#define RK3288_IMEM_SZIE                0x00018000

#define RK3036_IMEM_PHYS		0x10080000
#define RK3036_IMEM_SIZE		SZ_8K
#define RK3036_ROM_PHYS			0x10100000
#define RK3036_ROM_SIZE			SZ_16K
#define RK3036_CPU_AXI_BUS_PHYS		0x10128000
#define RK3036_CPU_AXI_BUS_SIZE		SZ_32K
#define RK3036_GIC_DIST_PHYS		0x10139000
#define RK3036_GIC_DIST_SIZE		SZ_4K
#define RK3036_GIC_CPU_PHYS		0x1013a000
#define RK3036_GIC_CPU_SIZE		SZ_4K
#define RK3036_CRU_PHYS			0x20000000
#define RK3036_CRU_SIZE			SZ_4K
#define RK3036_DDR_PCTL_PHYS		0x20004000
#define RK3036_DDR_PCTL_SIZE		SZ_4K
#define RK3036_GRF_PHYS			0x20008000
#define RK3036_GRF_SIZE			SZ_4K
#define RK3036_DDR_PHY_PHYS		0x2000a000
#define RK3036_DDR_PHY_SIZE		SZ_4K
#define RK3036_TIMER_PHYS		0x20044000
#define RK3036_TIMER_SIZE		SZ_4K
#define RK3036_UART0_PHYS		0x20060000
#define RK3036_UART1_PHYS		0x20064000
#define RK3036_UART2_PHYS		0x20068000
#define RK3036_UART_SIZE		SZ_4K
#define RK3036_GPIO0_PHYS		0x2007c000
#define RK3036_GPIO1_PHYS		0x20080000
#define RK3036_GPIO2_PHYS		0x20084000
#define RK3036_GPIO_SIZE		SZ_4K
#define RK3036_EFUSE_PHYS		0x20090000
#define RK3036_EFUSE_SIZE		SZ_4K

#define RK312X_IMEM_PHYS                RK3036_IMEM_PHYS
#define RK312X_IMEM_SIZE                RK3036_IMEM_SIZE
#define RK312X_ROM_PHYS                	RK3036_ROM_PHYS
#define RK312X_ROM_SIZE                 RK3036_ROM_SIZE
#define RK312X_CPU_AXI_BUS_PHYS         RK3036_CPU_AXI_BUS_PHYS
#define RK312X_CPU_AXI_BUS_SIZE         RK3036_CPU_AXI_BUS_SIZE
#define RK312X_GIC_DIST_PHYS            RK3036_GIC_DIST_PHYS
#define RK312X_GIC_DIST_SIZE            RK3036_GIC_DIST_SIZE
#define RK312X_GIC_CPU_PHYS             RK3036_GIC_CPU_PHYS
#define RK312X_GIC_CPU_SIZE             RK3036_GIC_CPU_SIZE
#define RK312X_CRU_PHYS                 RK3036_CRU_PHYS
#define RK312X_CRU_SIZE                 RK3036_CRU_SIZE
#define RK312X_DDR_PCTL_PHYS            RK3036_DDR_PCTL_PHYS
#define RK312X_DDR_PCTL_SIZE            RK3036_DDR_PCTL_SIZE
#define RK312X_GRF_PHYS                 RK3036_GRF_PHYS
#define RK312X_GRF_SIZE                 RK3036_GRF_SIZE
#define RK312X_DDR_PHY_PHYS             RK3036_DDR_PHY_PHYS
#define RK312X_DDR_PHY_SIZE             RK3036_DDR_PHY_SIZE
#define RK312X_TIMER_PHYS               RK3036_TIMER_PHYS
#define RK312X_TIMER_SIZE               RK3036_TIMER_SIZE
#define RK312X_UART0_PHYS               RK3036_UART0_PHYS
#define RK312X_UART1_PHYS               RK3036_UART1_PHYS
#define RK312X_UART2_PHYS               RK3036_UART2_PHYS
#define RK312X_UART_SIZE                RK3036_UART_SIZE
#define RK312X_GPIO0_PHYS               RK3036_GPIO0_PHYS
#define RK312X_GPIO1_PHYS               RK3036_GPIO1_PHYS
#define RK312X_GPIO2_PHYS               RK3036_GPIO2_PHYS
#define RK312X_GPIO_SIZE                RK3036_GPIO_SIZE
#define RK312X_EFUSE_PHYS               RK3036_EFUSE_PHYS
#define RK312X_EFUSE_SIZE               RK3036_EFUSE_SIZE

#endif
