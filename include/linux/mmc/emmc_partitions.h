#ifndef _EMMC_PARTITIONS_H
#define _EMMC_PARTITIONS_H

#include<linux/genhd.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>

#include <mach/register.h>
#include <mach/am_regs.h>

#define 	STORE_CODE 				1
#define	STORE_CACHE				(1<<1)
#define 	STORE_DATA				(1<<2)

#define     MAX_PART_NAME_LEN               16
#define     MAX_MMC_PART_NUM                16
#define     MMC_PARTITIONS_MAGIC            "MPT" // MMC Partition Table
#define     MMC_RESERVED_NAME               "reserved"

#define     SZ_1M                           0x00100000
#define     MMC_BOOT_PARTITION_SIZE         (4*SZ_1M) // the size of bootloader partition
#define     MMC_BOOT_PARTITION_RESERVED     (32*SZ_1M) // the size of reserve space behind bootloader partition

#define     RESULT_OK                       0
#define     RESULT_FAIL                     1
#define     RESULT_UNSUP_HOST               2
#define     RESULT_UNSUP_CARD               3

struct partitions {
    char name[MAX_PART_NAME_LEN];            /* identifier string */
    uint64_t size;            /* partition size, byte unit */
    uint64_t offset;        /* offset within the master space, byte unit */
    unsigned mask_flags;        /* master flags to mask out for this partition */
};

struct mmc_partitions_fmt {
    char magic[4];
    unsigned char version[12];
    int part_num;
    int checksum;
    struct partitions partitions[MAX_MMC_PART_NUM];
};

int aml_emmc_partition_ops (struct mmc_card *card, struct gendisk *disk);
unsigned int mmc_capacity (struct mmc_card *card);
int mmc_read_internal (struct mmc_card *card, unsigned dev_addr, unsigned blocks, void *buf);
int mmc_write_internal (struct mmc_card *card, unsigned dev_addr, unsigned blocks, void *buf);
int get_reserve_partition_off_from_tbl (void);

#endif
