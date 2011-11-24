/*
 * Copyright (c) 2006-2010 Atheros Communications, Inc.
 * All rights reserved.
 * 
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 * 
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include <a_config.h>
#include <a_osapi.h>
#include <athdefs.h>
#include <a_types.h>
#include <wmi.h>
#include "athdrv_linux.h"
#include "targaddrs.h"
#include "bmi_msg.h"
#include "AR6002/addrs.h"
/* Write Board Data from a Host file to the correct place in Target RAM. */

#if 0
#define DEBUG printf
#else
#define DEBUG(...)
#endif

#define MAC_ADDR_LEN              6

const char *prog_name;
A_UINT32 target_version;
A_UINT32 target_type;
unsigned char BMI_read_mem_cmd[12];
unsigned char BMI_write_mem_cmd[BMI_DATASZ_MAX+3*sizeof(A_UINT32)];
unsigned char mac_addr[MAC_ADDR_LEN];
unsigned int new_reg_val;
unsigned char new_reg[2];

char ifname[IFNAMSIZ];
int s; /* socket to Target */
struct ifreq ifr;
#define BOARD_DATA_SZ_MAX 2048
unsigned int bddata_size;

/* Command-line arguments specified */
A_BOOL file_specified = FALSE;
char *p_mac = NULL;
char *p_reg = NULL;

#define MAX_FILENAME 1023
char filename[MAX_FILENAME+1];
FILE *file;

const char cmd_args[] =
"arguments:\n\
  --setmac (update mac addr in file)\n\
  --setreg (update regulatory data in file)\n\
  --file filename\n\
  --interface interface_name   (e.g. eth1)\n\
\n";

#define ERROR(args...) \
do { \
    fprintf(stderr, "%s: ", prog_name); \
    fprintf(stderr, args); \
    exit(1); \
} while (0)

void
usage(void)
{
    fprintf(stderr, "usage:\n%s arguments...\n", prog_name);
    fprintf(stderr, "%s\n", cmd_args);
    exit(1);
}

/* Open the input file for reading and return its size */
unsigned int
open_input_file(void)
{
    struct stat statbuf;

    if (stat(filename, &statbuf) < 0) {
        ERROR("Cannot find file, %s\n", filename);
    }

    if (statbuf.st_size == 0) {
        ERROR("Empty Board Data file, %s\n", filename);
    }

    file = fopen(filename, "r");
    if (!file) {
        ERROR("Cannot read input file, %s\n", filename);
    }

    return statbuf.st_size;
}

/* Wait for the Target to start. */
void
wait_for_target(void)
{
    unsigned char *buffer;

    DEBUG("wait_for_target\n");

    /* Verify that the Target is alive.  If not, wait for it. */
    {
        int rv;
        static int waiting_msg_printed = 0;

        buffer = (unsigned char *)malloc(12);
        ((int *)buffer)[0] = AR6000_XIOCTL_TARGET_INFO;
        ifr.ifr_data = (char *)buffer;
        while ((rv=ioctl(s, AR6000_IOCTL_EXTENDED, &ifr)) < 0)
        {
            if (errno == ENODEV) {
                /* 
                 * Give the Target device a chance to start.
                 * Then loop back and see if it's alive.
                 */
                if (!waiting_msg_printed) {
                    printf("%s is waiting for Target....\n", prog_name);
                    waiting_msg_printed = 1;
                }
                usleep(1000000); /* sleep 1 Second */
            } else {
                break; /* Some unexpected error */
            }
        }
        target_version = *((A_UINT32 *)(&buffer[0]));
        target_type = *((A_UINT32 *)(&buffer[4]));
        DEBUG("Target version/type is 0x%x/0x%x\n", target_version, target_type);
        free(buffer);
    }
    DEBUG("Target is ready.....proceed\n");
}

/* Read Target memory word and return its value. */
void
BMI_read_mem(A_UINT32 address, A_UINT32 *pvalue)
{
    DEBUG("BMI_read_mem address=0x%x\n", address);

    ((int *)BMI_read_mem_cmd)[0] = AR6000_XIOCTL_BMI_READ_MEMORY;
    ((int *)BMI_read_mem_cmd)[1] = address;
    ((int *)BMI_read_mem_cmd)[2] = sizeof(A_UINT32);
    ifr.ifr_data = (char *)BMI_read_mem_cmd;
    if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
    {
        err(1, "%s", ifr.ifr_name);
    }
    *pvalue = ((int *)BMI_read_mem_cmd)[0];

    DEBUG("BMI_read_mem value read=0x%x\n", *pvalue);
}

/* Write a word to a Target memory. */
void
BMI_write_mem(A_UINT32 address, A_UINT8 *data, A_UINT32 sz)
{
    int chunk_sz;

    while (sz) {
        chunk_sz = (BMI_DATASZ_MAX > sz) ? sz : BMI_DATASZ_MAX;
        DEBUG("BMI_write_mem address=0x%x data=%p sz=%d\n",
                address, data, chunk_sz);

        ((int *)BMI_write_mem_cmd)[0] = AR6000_XIOCTL_BMI_WRITE_MEMORY;
        ((int *)BMI_write_mem_cmd)[1] = address;
        ((int *)BMI_write_mem_cmd)[2] = chunk_sz;
        memcpy(&((int *)BMI_write_mem_cmd)[3], data, chunk_sz);
        ifr.ifr_data = (char *)BMI_write_mem_cmd;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }

        sz -= chunk_sz;
        data += chunk_sz;
        address += chunk_sz;
    }
}

static int
wmic_ether_aton(const char *orig, A_UINT8 *eth)
{
    const char *bufp;   
    int i;

    i = 0;
    for (bufp = orig; *bufp != '\0'; ++bufp) {
        unsigned int val;
        unsigned char c = *bufp++;
        if (isdigit(c)) val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else break;

        val <<= 4;
        c = *bufp++;
        if (isdigit(c)) val |= c - '0';
        else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
        else break;

        eth[i] = (unsigned char) (val & 0377);
        if(++i == MAC_ADDR_LEN) {
            /* That's it.  Any trailing junk? */
            if (*bufp != '\0') {
                return 0;
            }
            return 1;
        }
        if (*bufp != ':') {
                break;
        }
    }
    return 0;
}


/* Process command-line arguments */
void
scan_args(int argc, char **argv)
{
    int c;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"transfer", 0, NULL, 't'}, /*  backwards compat */
            {"setmac", 1, NULL, 's'},
            {"setreg", 1, NULL, 'd'},
            {"file", 1, NULL, 'f'},
            {"interface", 1, NULL, 'i'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "sf:i:",
                         long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 't':
           break;

        case 's': /* Update MAC address in the file */
            p_mac = optarg;
            break;

        case 'd':
            p_reg = optarg;
            /*
             * Get the regulatory numbers from cmd line, and store them in an
             * array of 2 bytes.  This is the only way to maintain sanity in a system
             * where the elements are packed and may not be word-aligned, because
             * if I do it this way, I can use memcpy to copy the regulatory data
             * into host RAM.
             */
            sscanf(p_reg, "%x", &new_reg_val); 
            new_reg[0] = new_reg_val & (0xFF);
            new_reg[1] = (new_reg_val & (0xFF00)) >> 8;
            break;

        case 'f': /* Filename to use with --read or --write */
            memset(filename, '\0', MAX_FILENAME+1);
            strncpy(filename, optarg, MAX_FILENAME);
            file_specified = TRUE;
            break;

        case 'i': /* interface name */
            memset(ifname, '\0', IFNAMSIZ);
            strncpy(ifname, optarg, IFNAMSIZ-1);
            break;

        default:
            usage();
        }
    }

    if (p_mac) {
        if (!wmic_ether_aton(p_mac, mac_addr)) {
            ERROR("invalid MAC address in option --setmac\n");
        }
    }

    if (file_specified) {
        bddata_size = open_input_file();
    } else {
        ERROR("Must specify filename of Board Data\n");
    }
}

/*
 * Update the regulatory data in host RAM
 */
static void
update_reg(unsigned char* eeprom, A_UINT32 sz, unsigned char* new_reg_ptr)
{
    A_UINT32 i;
    A_UINT16 *ptr_checksum;
    A_UINT16 *ptr_eeprom;
    A_UINT16 checksum;
    unsigned char *ptr_reg;

    if (target_type == TARGET_TYPE_AR6003) {
        ptr_checksum = (A_UINT16 *)(eeprom + 4);
        ptr_reg  = eeprom + 12;
    } else {
      ERROR("Updating regulatory domain only supported on AR6003");
    }

    memcpy(ptr_reg, new_reg_ptr, 2); // Regulatory domain is 2 bytes
 
    /* Clear current checksum and recalculate it */ 
    *ptr_checksum = 0;
    ptr_eeprom = (A_UINT16*)eeprom;

    checksum = 0;
    for (i=0;i<sz;i+=2) {
        checksum ^= *ptr_eeprom;
        ptr_eeprom++;
    }
    checksum = ~checksum;

    *ptr_checksum = checksum;
    return;
}

/*
 * Update the MAC address in host RAM
 */
static void
update_mac(unsigned char* eeprom, A_UINT32 sz, unsigned char* macaddr)
{
    A_UINT32 i;
    A_UINT16 *ptr_checksum;
    A_UINT16 *ptr_eeprom;
    A_UINT16 checksum;
    unsigned char *ptr_macaddr;
   
    if (target_type == TARGET_TYPE_AR6001) {
        ptr_checksum = (A_UINT16 *)(eeprom + 0);
        ptr_macaddr  = eeprom + 6;
    } else if (target_type == TARGET_TYPE_AR6002) {
        ptr_checksum = (A_UINT16 *)(eeprom + 4);
        ptr_macaddr  = eeprom + 10;
    } else if (target_type == TARGET_TYPE_AR6003) {
        ptr_checksum = (A_UINT16 *)(eeprom + 4);
        ptr_macaddr  = eeprom + 22;
    } else if (target_type == TARGET_TYPE_MCKINLEY) {
        ptr_checksum = (A_UINT16 *)(eeprom + 4);
        ptr_macaddr  = eeprom + 22;
    } else {
        ERROR("invalid target type \n");
    }

    memcpy(ptr_macaddr,macaddr,MAC_ADDR_LEN);

    /* Clear current checksum and recalculate it */ 
    *ptr_checksum = 0;
    ptr_eeprom = (A_UINT16*)eeprom;

    checksum = 0;
    for (i=0;i<sz;i+=2) {
        checksum ^= *ptr_eeprom;
        ptr_eeprom++;
    }
    checksum = ~checksum;

    *ptr_checksum = checksum;
    return;
}

void 
target_get_bd_sz(A_UINT32 target_type, A_UINT32 *bd_size, A_UINT32 *bd_ext_size)
{
    if (target_type == TARGET_TYPE_AR6002) {
        *bd_size = AR6002_BOARD_DATA_SZ;
        *bd_ext_size = AR6002_BOARD_EXT_DATA_SZ;
    } else if (target_type == TARGET_TYPE_AR6003) {
        *bd_size = AR6003_BOARD_DATA_SZ;
        *bd_ext_size = AR6003_BOARD_EXT_DATA_SZ;
    } else if (target_type == TARGET_TYPE_MCKINLEY) {
        *bd_size = MCKINLEY_BOARD_DATA_SZ;
        *bd_ext_size = MCKINLEY_BOARD_EXT_DATA_SZ;
    }

    return;
}

int
main(int argc, char *argv[])
{
    A_UINT32 board_data_addr;
    A_UCHAR eeprom_data[BOARD_DATA_SZ_MAX];
    A_UCHAR eeprom_ext_data[BOARD_DATA_SZ_MAX];
    struct stat fstat_buf;
    A_BOOL ext_data_avail;
    A_UINT32 bd_size, bd_ext_size;

    prog_name = argv[0];
    strcpy(ifname, "eth1"); /* default ifname */

    scan_args(argc, argv);

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) err(1, "socket");
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    wait_for_target();
    ext_data_avail = FALSE;
    /*
     * Transfer from file to Target RAM.
     * Fetch source data from file.
     */
    target_get_bd_sz(target_type, &bd_size, &bd_ext_size);
    if (fread(eeprom_data, 1, bd_size, file) != bd_size) {
       ERROR("Read from local file failed\n");
    }
    if (fstat(fileno(file), &fstat_buf)) {
       ERROR("File stats for local file failed\n");
    }
    if ((fstat_buf.st_size > bd_size) && (bd_ext_size)) {
        
        /*if use 1792 size bin file in Venus 2.1.1, the bd_ext_size needs adjust to 768*/
        if ((fstat_buf.st_size - bd_size) < bd_ext_size) {
            bd_ext_size = fstat_buf.st_size - bd_size;
        }    
        if ((bd_ext_size) &&
            (fread(eeprom_ext_data, 1, bd_ext_size, file) != bd_ext_size)) 
        {
            ERROR("Read from local file failed for extended data\n");
        }
        ext_data_avail = TRUE;
    }
    /* Determine where in Target RAM to write Board Data */
    BMI_read_mem(HOST_INTEREST_ITEM_ADDRESS(target_type, hi_board_data),
                 &board_data_addr);
    if (board_data_addr == 0) {
        ERROR("hi_board_data is zero\n");
    }

    /* Update MAC address in RAM */
    if (p_mac) {
        update_mac(eeprom_data, bd_size, mac_addr);
    }

    if (p_reg) {
        update_reg(eeprom_data, bd_size, new_reg);
    }

    /* Write EEPROM data to Target RAM */
    BMI_write_mem(board_data_addr, ((A_UINT8 *)eeprom_data), bd_size);

    /* Record the fact that Board Data IS initialized */
    {
        A_UINT32 one = 1;
        BMI_write_mem(HOST_INTEREST_ITEM_ADDRESS(target_type, hi_board_data_initialized),
                (A_UINT8 *)&one, sizeof(A_UINT32));
    }

    if (ext_data_avail) {
        /* Determine where in Target RAM to write Board Data */
        BMI_read_mem(HOST_INTEREST_ITEM_ADDRESS(target_type, hi_board_ext_data),
                 &board_data_addr);
        if (board_data_addr == 0) {
            ERROR("hi_board_ext_data is zero\n");
        }

        /* Write EEPROM data to Target RAM */
        BMI_write_mem(board_data_addr, ((A_UINT8 *)eeprom_ext_data), bd_ext_size);

        /* Record the fact that Board Data IS initialized */
        {
            A_UINT32 config;
            config = bd_ext_size << 16 | 1;
            BMI_write_mem(HOST_INTEREST_ITEM_ADDRESS(target_type, hi_board_ext_data_config),
                 (A_UINT8 *)&config, sizeof(A_UINT32));
        }
    }

    return 0;
}
