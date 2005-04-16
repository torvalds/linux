#ifndef __ASM_AXISFLASHMAP_H
#define __ASM_AXISFLASHMAP_H

/* Bootblock parameters are stored at 0xc000 and has the FLASH_BOOT_MAGIC 
 * as start, it ends with 0xFFFFFFFF */
#define FLASH_BOOT_MAGIC 0xbeefcace
#define BOOTPARAM_OFFSET 0xc000
/* apps/bootblocktool is used to read and write the parameters,
 * and it has nothing to do with the partition table. 
 */

#define PARTITION_TABLE_OFFSET 10
#define PARTITION_TABLE_MAGIC 0xbeef /* Not a good magic */

/* The partitiontable_head is located at offset +10: */
struct partitiontable_head {
	__u16 magic; /* PARTITION_TABLE_MAGIC */ 
	__u16 size;  /* Length of ptable block (not header) */
	__u32 checksum; /* simple longword sum */
};

/* And followed by partition table entries */
struct partitiontable_entry {
	__u32 offset;   /* Offset is relative to the sector the ptable is in */
	__u32 size;
	__u32 checksum; /* simple longword sum */
	__u16 type;
	__u16 flags;   /* bit 0: ro/rw = 1/0 */
	__u32 future0; /* 16 bytes reserved for future use */
	__u32 future1;
	__u32 future2;
	__u32 future3;
};
/* ended by an end marker: */
#define PARTITIONTABLE_END_MARKER 0xFFFFFFFF
#define PARTITIONTABLE_END_MARKER_SIZE 4

/*#define PARTITION_TYPE_RESCUE 0x0000?*/  /* Not used, maybe it should? */
#define PARTITION_TYPE_PARAM  0x0001
#define PARTITION_TYPE_KERNEL 0x0002
#define PARTITION_TYPE_JFFS   0x0003

#endif
