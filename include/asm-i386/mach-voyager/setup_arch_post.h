/* Hook for machine specific memory setup.
 *
 * This is included late in kernel/setup.c so that it can make use of all of
 * the static functions. */

static char * __init machine_specific_memory_setup(void)
{
	char *who;

	who = "NOT VOYAGER";

	if(voyager_level == 5) {
		__u32 addr, length;
		int i;

		who = "Voyager-SUS";

		e820.nr_map = 0;
		for(i=0; voyager_memory_detect(i, &addr, &length); i++) {
			add_memory_region(addr, length, E820_RAM);
		}
		return who;
	} else if(voyager_level == 4) {
		__u32 tom;
		__u16 catbase = inb(VOYAGER_SSPB_RELOCATION_PORT)<<8;
		/* select the DINO config space */
		outb(VOYAGER_DINO, VOYAGER_CAT_CONFIG_PORT);
		/* Read DINO top of memory register */
		tom = ((inb(catbase + 0x4) & 0xf0) << 16)
			+ ((inb(catbase + 0x5) & 0x7f) << 24);

		if(inb(catbase) != VOYAGER_DINO) {
			printk(KERN_ERR "Voyager: Failed to get DINO for L4, setting tom to EXT_MEM_K\n");
			tom = (EXT_MEM_K)<<10;
		}
		who = "Voyager-TOM";
		add_memory_region(0, 0x9f000, E820_RAM);
		/* map from 1M to top of memory */
		add_memory_region(1*1024*1024, tom - 1*1024*1024, E820_RAM);
		/* FIXME: Should check the ASICs to see if I need to
		 * take out the 8M window.  Just do it at the moment
		 * */
		add_memory_region(8*1024*1024, 8*1024*1024, E820_RESERVED);
		return who;
	}

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
