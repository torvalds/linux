#include <linux/spinlock.h>

struct cpustate_t {
	spinlock_t lock;
	int excl;
        int open_count;
	unsigned char cached_val;
	int inited;
	unsigned long *set_addr;
	unsigned long *clr_addr;
};


#define HDPU_CPUSTATE_NAME "hdpu cpustate"
#define HDPU_NEXUS_NAME "hdpu nexus"

#define CPUSTATE_KERNEL_MAJOR  0x10

#define CPUSTATE_KERNEL_INIT_DRV   0 /* CPU State Driver Initialized */
#define CPUSTATE_KERNEL_INIT_PCI   1 /* 64360 PCI Busses Init */
#define CPUSTATE_KERNEL_INIT_REG   2 /* 64360 Bridge Init */
#define CPUSTATE_KERNEL_CPU1_KICK  3 /* Boot cpu 1 */
#define CPUSTATE_KERNEL_CPU1_OK    4  /* Cpu 1 has checked in */
#define CPUSTATE_KERNEL_OK         5 /* Terminal state */
#define CPUSTATE_KERNEL_RESET   14 /* Board reset via SW*/
#define CPUSTATE_KERNEL_HALT   15 /* Board halted via SW*/
