#ifndef __LINUX_PATA_PLATFORM_H
#define __LINUX_PATA_PLATFORM_H

struct pata_platform_info {
	/*
	 * I/O port shift, for platforms with ports that are
	 * constantly spaced and need larger than the 1-byte
	 * spacing used by ata_std_ports().
	 */
	unsigned int ioport_shift;
};

#endif /* __LINUX_PATA_PLATFORM_H */
