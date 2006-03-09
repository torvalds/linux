/* $Id: oplib.h,v 1.14 2001/12/19 00:29:51 davem Exp $
 * oplib.h:  Describes the interface and available routines in the
 *           Linux Prom library.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef __SPARC64_OPLIB_H
#define __SPARC64_OPLIB_H

#include <linux/config.h>
#include <asm/openprom.h>

/* OBP version string. */
extern char prom_version[];

/* Root node of the prom device tree, this stays constant after
 * initialization is complete.
 */
extern int prom_root_node;

/* PROM stdin and stdout */
extern int prom_stdin, prom_stdout;

/* /chosen node of the prom device tree, this stays constant after
 * initialization is complete.
 */
extern int prom_chosen_node;

/* Helper values and strings in arch/sparc64/kernel/head.S */
extern const char prom_peer_name[];
extern const char prom_compatible_name[];
extern const char prom_root_compatible[];
extern const char prom_finddev_name[];
extern const char prom_chosen_path[];
extern const char prom_getprop_name[];
extern const char prom_mmu_name[];
extern const char prom_callmethod_name[];
extern const char prom_translate_name[];
extern const char prom_map_name[];
extern const char prom_unmap_name[];
extern int prom_mmu_ihandle_cache;
extern unsigned int prom_boot_mapped_pc;
extern unsigned int prom_boot_mapping_mode;
extern unsigned long prom_boot_mapping_phys_high, prom_boot_mapping_phys_low;

struct linux_mlist_p1275 {
	struct linux_mlist_p1275 *theres_more;
	unsigned long start_adr;
	unsigned long num_bytes;
};

struct linux_mem_p1275 {
	struct linux_mlist_p1275 **p1275_totphys;
	struct linux_mlist_p1275 **p1275_prommap;
	struct linux_mlist_p1275 **p1275_available; /* What we can use */
};

/* The functions... */

/* You must call prom_init() before using any of the library services,
 * preferably as early as possible.  Pass it the romvec pointer.
 */
extern void prom_init(void *cif_handler, void *cif_stack);

/* Boot argument acquisition, returns the boot command line string. */
extern char *prom_getbootargs(void);

/* Device utilities. */

/* Device operations. */

/* Open the device described by the passed string.  Note, that the format
 * of the string is different on V0 vs. V2->higher proms.  The caller must
 * know what he/she is doing!  Returns the device descriptor, an int.
 */
extern int prom_devopen(const char *device_string);

/* Close a previously opened device described by the passed integer
 * descriptor.
 */
extern int prom_devclose(int device_handle);

/* Do a seek operation on the device described by the passed integer
 * descriptor.
 */
extern void prom_seek(int device_handle, unsigned int seek_hival,
		      unsigned int seek_lowval);

/* Miscellaneous routines, don't really fit in any category per se. */

/* Reboot the machine with the command line passed. */
extern void prom_reboot(const char *boot_command);

/* Evaluate the forth string passed. */
extern void prom_feval(const char *forth_string);

/* Enter the prom, with possibility of continuation with the 'go'
 * command in newer proms.
 */
extern void prom_cmdline(void);

/* Enter the prom, with no chance of continuation for the stand-alone
 * which calls this.
 */
extern void prom_halt(void) __attribute__ ((noreturn));

/* Halt and power-off the machine. */
extern void prom_halt_power_off(void) __attribute__ ((noreturn));

/* Set the PROM 'sync' callback function to the passed function pointer.
 * When the user gives the 'sync' command at the prom prompt while the
 * kernel is still active, the prom will call this routine.
 *
 */
typedef int (*callback_func_t)(long *cmd);
extern void prom_setcallback(callback_func_t func_ptr);

/* Acquire the IDPROM of the root node in the prom device tree.  This
 * gets passed a buffer where you would like it stuffed.  The return value
 * is the format type of this idprom or 0xff on error.
 */
extern unsigned char prom_get_idprom(char *idp_buffer, int idpbuf_size);

/* Character operations to/from the console.... */

/* Non-blocking get character from console. */
extern int prom_nbgetchar(void);

/* Non-blocking put character to console. */
extern int prom_nbputchar(char character);

/* Blocking get character from console. */
extern char prom_getchar(void);

/* Blocking put character to console. */
extern void prom_putchar(char character);

/* Prom's internal routines, don't use in kernel/boot code. */
extern void prom_printf(const char *fmt, ...);
extern void prom_write(const char *buf, unsigned int len);

/* Query for input device type */

enum prom_input_device {
	PROMDEV_IKBD,			/* input from keyboard */
	PROMDEV_ITTYA,			/* input from ttya */
	PROMDEV_ITTYB,			/* input from ttyb */
	PROMDEV_IRSC,			/* input from rsc */
	PROMDEV_IVCONS,			/* input from virtual-console */
	PROMDEV_I_UNK,
};

extern enum prom_input_device prom_query_input_device(void);

/* Query for output device type */

enum prom_output_device {
	PROMDEV_OSCREEN,		/* to screen */
	PROMDEV_OTTYA,			/* to ttya */
	PROMDEV_OTTYB,			/* to ttyb */
	PROMDEV_ORSC,			/* to rsc */
	PROMDEV_OVCONS,			/* to virtual-console */
	PROMDEV_O_UNK,
};

extern enum prom_output_device prom_query_output_device(void);

/* Multiprocessor operations... */
#ifdef CONFIG_SMP
/* Start the CPU with the given device tree node at the passed program
 * counter with the given arg passed in via register %o0.
 */
extern void prom_startcpu(int cpunode, unsigned long pc, unsigned long arg);

/* Start the CPU with the given cpu ID at the passed program
 * counter with the given arg passed in via register %o0.
 */
extern void prom_startcpu_cpuid(int cpuid, unsigned long pc, unsigned long arg);

/* Stop the CPU with the given cpu ID.  */
extern void prom_stopcpu_cpuid(int cpuid);

/* Stop the current CPU. */
extern void prom_stopself(void);

/* Idle the current CPU. */
extern void prom_idleself(void);

/* Resume the CPU with the passed device tree node. */
extern void prom_resumecpu(int cpunode);
#endif

/* Power management interfaces. */

/* Put the current CPU to sleep. */
extern void prom_sleepself(void);

/* Put the entire system to sleep. */
extern int prom_sleepsystem(void);

/* Initiate a wakeup event. */
extern int prom_wakeupsystem(void);

/* MMU and memory related OBP interfaces. */

/* Get unique string identifying SIMM at given physical address. */
extern int prom_getunumber(int syndrome_code,
			   unsigned long phys_addr,
			   char *buf, int buflen);

/* Retain physical memory to the caller across soft resets. */
extern unsigned long prom_retain(const char *name,
				 unsigned long pa_low, unsigned long pa_high,
				 long size, long align);

/* Load explicit I/D TLB entries into the calling processor. */
extern long prom_itlb_load(unsigned long index,
			   unsigned long tte_data,
			   unsigned long vaddr);

extern long prom_dtlb_load(unsigned long index,
			   unsigned long tte_data,
			   unsigned long vaddr);

/* Map/Unmap client program address ranges.  First the format of
 * the mapping mode argument.
 */
#define PROM_MAP_WRITE	0x0001 /* Writable */
#define PROM_MAP_READ	0x0002 /* Readable - sw */
#define PROM_MAP_EXEC	0x0004 /* Executable - sw */
#define PROM_MAP_LOCKED	0x0010 /* Locked, use i/dtlb load calls for this instead */
#define PROM_MAP_CACHED	0x0020 /* Cacheable in both L1 and L2 caches */
#define PROM_MAP_SE	0x0040 /* Side-Effects */
#define PROM_MAP_GLOB	0x0080 /* Global */
#define PROM_MAP_IE	0x0100 /* Invert-Endianness */
#define PROM_MAP_DEFAULT (PROM_MAP_WRITE | PROM_MAP_READ | PROM_MAP_EXEC | PROM_MAP_CACHED)

extern int prom_map(int mode, unsigned long size,
		    unsigned long vaddr, unsigned long paddr);
extern void prom_unmap(unsigned long size, unsigned long vaddr);


/* PROM device tree traversal functions... */

#ifdef PROMLIB_INTERNAL

/* Internal version of prom_getchild. */
extern int __prom_getchild(int parent_node);

/* Internal version of prom_getsibling. */
extern int __prom_getsibling(int node);

#endif

/* Get the child node of the given node, or zero if no child exists. */
extern int prom_getchild(int parent_node);

/* Get the next sibling node of the given node, or zero if no further
 * siblings exist.
 */
extern int prom_getsibling(int node);

/* Get the length, at the passed node, of the given property type.
 * Returns -1 on error (ie. no such property at this node).
 */
extern int prom_getproplen(int thisnode, const char *property);

/* Fetch the requested property using the given buffer.  Returns
 * the number of bytes the prom put into your buffer or -1 on error.
 */
extern int prom_getproperty(int thisnode, const char *property,
			    char *prop_buffer, int propbuf_size);

/* Acquire an integer property. */
extern int prom_getint(int node, const char *property);

/* Acquire an integer property, with a default value. */
extern int prom_getintdefault(int node, const char *property, int defval);

/* Acquire a boolean property, 0=FALSE 1=TRUE. */
extern int prom_getbool(int node, const char *prop);

/* Acquire a string property, null string on error. */
extern void prom_getstring(int node, const char *prop, char *buf, int bufsize);

/* Does the passed node have the given "name"? YES=1 NO=0 */
extern int prom_nodematch(int thisnode, const char *name);

/* Puts in buffer a prom name in the form name@x,y or name (x for which_io 
 * and y for first regs phys address
 */
extern int prom_getname(int node, char *buf, int buflen);

/* Search all siblings starting at the passed node for "name" matching
 * the given string.  Returns the node on success, zero on failure.
 */
extern int prom_searchsiblings(int node_start, const char *name);

/* Return the first property type, as a string, for the given node.
 * Returns a null string on error. Buffer should be at least 32B long.
 */
extern char *prom_firstprop(int node, char *buffer);

/* Returns the next property after the passed property for the given
 * node.  Returns null string on failure. Buffer should be at least 32B long.
 */
extern char *prom_nextprop(int node, const char *prev_property, char *buffer);

/* Returns 1 if the specified node has given property. */
extern int prom_node_has_property(int node, const char *property);

/* Returns phandle of the path specified */
extern int prom_finddevice(const char *name);

/* Set the indicated property at the given node with the passed value.
 * Returns the number of bytes of your value that the prom took.
 */
extern int prom_setprop(int node, const char *prop_name, char *prop_value,
			int value_size);
			
extern int prom_pathtoinode(const char *path);
extern int prom_inst2pkg(int);

/* CPU probing helpers.  */
int cpu_find_by_instance(int instance, int *prom_node, int *mid);
int cpu_find_by_mid(int mid, int *prom_node);

/* Client interface level routines. */
extern void prom_set_trap_table(unsigned long tba);
extern void prom_set_trap_table_sun4v(unsigned long tba, unsigned long mmfsa);

extern long p1275_cmd(const char *, long, ...);
				   

#if 0
#define P1275_SIZE(x) ((((long)((x) / 32)) << 32) | (x))
#else
#define P1275_SIZE(x) x
#endif

/* We support at most 16 input and 1 output argument */
#define P1275_ARG_NUMBER		0
#define P1275_ARG_IN_STRING		1
#define P1275_ARG_OUT_BUF		2
#define P1275_ARG_OUT_32B		3
#define P1275_ARG_IN_FUNCTION		4
#define P1275_ARG_IN_BUF		5
#define P1275_ARG_IN_64B		6

#define P1275_IN(x) ((x) & 0xf)
#define P1275_OUT(x) (((x) << 4) & 0xf0)
#define P1275_INOUT(i,o) (P1275_IN(i)|P1275_OUT(o))
#define P1275_ARG(n,x) ((x) << ((n)*3 + 8))

#endif /* !(__SPARC64_OPLIB_H) */
