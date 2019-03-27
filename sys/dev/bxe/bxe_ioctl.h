/*
 * Copyright (c) 2015-2016 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _BXE_IOCTL_H_
#define _BXE_IOCTL_H_

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioccom.h>


struct bxe_grcdump {
    uint16_t pci_func;
    uint32_t grcdump_size;
    void *grcdump;
    uint32_t grcdump_dwords;
};
typedef struct bxe_grcdump bxe_grcdump_t;

#define BXE_DRV_NAME_LENGTH             32
#define BXE_DRV_VERSION_LENGTH          32
#define BXE_MFW_VERSION_LENGTH          32
#define BXE_STORMFW_VERSION_LENGTH      32
#define BXE_BUS_INFO_LENGTH             32

struct bxe_drvinfo {
    char drv_name[BXE_DRV_NAME_LENGTH];
    char drv_version[BXE_DRV_VERSION_LENGTH];
    char mfw_version[BXE_MFW_VERSION_LENGTH];
    char stormfw_version[BXE_STORMFW_VERSION_LENGTH];
    uint32_t eeprom_dump_len; /* in bytes */
    uint32_t reg_dump_len; /* in bytes */
    char bus_info[BXE_BUS_INFO_LENGTH];
};
typedef struct bxe_drvinfo bxe_drvinfo_t;

struct bxe_dev_setting {

    uint32_t supported;  /* Features this interface supports */
    uint32_t advertising;/* Features this interface advertises */
    uint32_t speed;      /* The forced speed, 10Mb, 100Mb, gigabit */
    uint32_t duplex;     /* Duplex, half or full */
    uint32_t port;       /* Which connector port */
    uint32_t phy_address;/* port number*/
    uint32_t autoneg;    /* Enable or disable autonegotiation */
};
typedef struct bxe_dev_setting bxe_dev_setting_t;

struct bxe_get_regs {
    void *reg_buf;
    uint32_t reg_buf_len;
};
typedef struct bxe_get_regs bxe_get_regs_t;

#define BXE_EEPROM_MAX_DATA_LEN   524288

struct bxe_eeprom {
    uint32_t eeprom_cmd;
#define BXE_EEPROM_CMD_SET_EEPROM       0x01
#define BXE_EEPROM_CMD_GET_EEPROM       0x02

    void *eeprom_data;
    uint32_t eeprom_offset;
    uint32_t eeprom_data_len;
    uint32_t eeprom_magic;
};
typedef struct bxe_eeprom bxe_eeprom_t;

struct bxe_reg_rdw {
    uint32_t reg_cmd;
#define BXE_READ_REG_CMD                0x01
#define BXE_WRITE_REG_CMD               0x02

    uint32_t reg_id;
    uint32_t reg_val;
    uint32_t reg_access_type;
#define BXE_REG_ACCESS_DIRECT           0x01
#define BXE_REG_ACCESS_INDIRECT         0x02
};

typedef struct bxe_reg_rdw bxe_reg_rdw_t;

struct bxe_pcicfg_rdw {
    uint32_t cfg_cmd;
#define BXE_READ_PCICFG                 0x01
#define BXE_WRITE_PCICFG                0x01
    uint32_t cfg_id;
    uint32_t cfg_val;
    uint32_t cfg_width;
};

typedef struct bxe_pcicfg_rdw bxe_pcicfg_rdw_t;

struct bxe_perm_mac_addr {
    char mac_addr_str[32];
};

typedef struct bxe_perm_mac_addr bxe_perm_mac_addr_t;


/*
 * Read grcdump size
 */
#define BXE_GRC_DUMP_SIZE     _IOWR('e', 1, bxe_grcdump_t)

/*
 * Read grcdump
 */
#define BXE_GRC_DUMP          _IOWR('e', 2, bxe_grcdump_t)

/*
 * Read driver info
 */
#define BXE_DRV_INFO          _IOR('e', 3, bxe_drvinfo_t)

/*
 * Read Device Setting
 */
#define BXE_DEV_SETTING       _IOR('e', 4, bxe_dev_setting_t)

/*
 * Get Registers
 */
#define BXE_GET_REGS          _IOR('e', 5, bxe_get_regs_t)

/*
 * Get/Set EEPROM
 */
#define BXE_EEPROM            _IOWR('e', 6, bxe_eeprom_t)

/*
 * read/write a register
 */
#define BXE_RDW_REG           _IOWR('e', 7, bxe_reg_rdw_t)

/*
 * read/write PCIcfg
 */
#define BXE_RDW_PCICFG        _IOWR('e', 8, bxe_reg_rdw_t)

/*
 * get permanent mac address
 */

#define BXE_MAC_ADDR          _IOWR('e', 9, bxe_perm_mac_addr_t)


#endif /* #ifndef _QLNX_IOCTL_H_ */
