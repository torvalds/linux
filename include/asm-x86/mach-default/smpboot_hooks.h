/* two abstractions specific to kernel/smpboot.c, mainly to cater to visws
 * which needs to alter them. */

static inline void smpboot_clear_io_apic_irqs(void)
{
	io_apic_irqs = 0;
}

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

static inline void smpboot_restore_warm_reset_vector(void)
{
	/*
	 * Install writable page 0 entry to set BIOS data area.
	 */
	local_flush_tlb();

	/*
	 * Paranoid:  Set warm reset code and vector here back
	 * to default values.
	 */
	CMOS_WRITE(0, 0xf);

	*((volatile long *) phys_to_virt(0x467)) = 0;
}

static inline void smpboot_setup_io_apic(void)
{
	/*
	 * Here we can be sure that there is an IO-APIC in the system. Let's
	 * go and set it up:
	 */
	if (!skip_ioapic_setup && nr_ioapics)
		setup_IO_APIC();
	else
		nr_ioapics = 0;
}

static inline void smpboot_clear_io_apic(void)
{
	nr_ioapics = 0;
}
