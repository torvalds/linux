#ifndef LINUX_SSB_MIPSCORE_H_
#define LINUX_SSB_MIPSCORE_H_

#ifdef CONFIG_SSB_DRIVER_MIPS

struct ssb_device;

struct ssb_serial_port {
	void *regs;
	unsigned long clockspeed;
	unsigned int irq;
	unsigned int baud_base;
	unsigned int reg_shift;
};

struct ssb_pflash {
	bool present;
	u8 buswidth;
	u32 window;
	u32 window_size;
};

struct ssb_mipscore {
	struct ssb_device *dev;

	int nr_serial_ports;
	struct ssb_serial_port serial_ports[4];

	struct ssb_pflash pflash;
};

extern void ssb_mipscore_init(struct ssb_mipscore *mcore);
extern u32 ssb_cpu_clock(struct ssb_mipscore *mcore);

extern unsigned int ssb_mips_irq(struct ssb_device *dev);


#else /* CONFIG_SSB_DRIVER_MIPS */

struct ssb_mipscore {
};

static inline
void ssb_mipscore_init(struct ssb_mipscore *mcore)
{
}

#endif /* CONFIG_SSB_DRIVER_MIPS */

#endif /* LINUX_SSB_MIPSCORE_H_ */
