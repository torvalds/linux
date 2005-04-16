/**
 * machine_specific_memory_setup - Hook for machine specific memory setup.
 *
 * Description:
 *	This is included late in kernel/setup.c so that it can make
 *	use of all of the static functions.
 **/

static char * __init machine_specific_memory_setup(void)
{
	char *who;


	who = "BIOS-e820";

	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	sanitize_e820_map(E820_MAP, &E820_MAP_NR);
	if (copy_e820_map(E820_MAP, E820_MAP_NR) < 0) {
		unsigned long mem_size;

		/* compare results from other methods and take the greater */
		if (ALT_MEM_K < EXT_MEM_K) {
			mem_size = EXT_MEM_K;
			who = "BIOS-88";
		} else {
			mem_size = ALT_MEM_K;
			who = "BIOS-e801";
		}

		e820.nr_map = 0;
		add_memory_region(0, LOWMEMSIZE(), E820_RAM);
		add_memory_region(HIGH_MEMORY, mem_size << 10, E820_RAM);
  	}
	return who;
}
