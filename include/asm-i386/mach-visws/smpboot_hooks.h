static inline void smpboot_setup_warm_reset_vector(unsigned long start_eip)
{
	CMOS_WRITE(0xa, 0xf);
	local_flush_tlb();
	Dprintk("1.\n");
	*((volatile unsigned short *) TRAMPOLINE_HIGH) = start_eip >> 4;
	Dprintk("2.\n");
	*((volatile unsigned short *) TRAMPOLINE_LOW) = start_eip & 0xf;
	Dprintk("3.\n");
}

/* for visws do nothing for any of these */

static inline void smpboot_clear_io_apic_irqs(void)
{
}

static inline void smpboot_restore_warm_reset_vector(void)
{
}

static inline void smpboot_setup_io_apic(void)
{
}
