/* Hook for machine specific memory setup.
 *
 * This is included late in kernel/setup.c so that it can make use of all of
 * the static functions. */

#define MB (1024 * 1024)

unsigned long sgivwfb_mem_phys;
unsigned long sgivwfb_mem_size;

long long mem_size __initdata = 0;

static char * __init machine_specific_memory_setup(void)
{
	long long gfx_mem_size = 8 * MB;

	mem_size = ALT_MEM_K;

	if (!mem_size) {
		printk(KERN_WARNING "Bootloader didn't set memory size, upgrade it !\n");
		mem_size = 128 * MB;
	}

	/*
	 * this hardcodes the graphics memory to 8 MB
	 * it really should be sized dynamically (or at least
	 * set as a boot param)
	 */
	if (!sgivwfb_mem_size) {
		printk(KERN_WARNING "Defaulting to 8 MB framebuffer size\n");
		sgivwfb_mem_size = 8 * MB;
	}

	/*
	 * Trim to nearest MB
	 */
	sgivwfb_mem_size &= ~((1 << 20) - 1);
	sgivwfb_mem_phys = mem_size - gfx_mem_size;

	add_memory_region(0, LOWMEMSIZE(), E820_RAM);
	add_memory_region(HIGH_MEMORY, mem_size - sgivwfb_mem_size - HIGH_MEMORY, E820_RAM);
	add_memory_region(sgivwfb_mem_phys, sgivwfb_mem_size, E820_RESERVED);

	return "PROM";

	/* Remove gcc warnings */
	(void) sanitize_e820_map(NULL, NULL);
	(void) copy_e820_map(NULL, 0);
}
