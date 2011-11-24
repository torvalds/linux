/*
 * Copyright (c) 2004-2006 Atheros Communications Inc.
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

#include <a_config.h>
#include <a_osapi.h>
#include <athdefs.h>
#include <a_types.h>
#include <wmi.h>
#include "athdrv_linux.h"
#include "bmi_msg.h"
#include "bmiloader.h"

#ifndef PATH_MAX
#define PATH_MAX (255)
#endif

#define ADDRESS_FLAG                    0x001
#define LENGTH_FLAG                     0x002
#define PARAM_FLAG                      0x004
#define FILE_FLAG                       0x008
#define COUNT_FLAG                      0x010
#define AND_OP_FLAG                     0x020
#define BITWISE_OP_FLAG                 0x040
#define QUIET_FLAG                      0x080
#define UNCOMPRESS_FLAG                 0x100
#define TIMEOUT_FLAG                    0x200
#define BMI_SYSFS_FLAG                  0x400
#define DEVICE_FLAG                     0x800

#define BMI_TEST                        BMI_NO_COMMAND

/* Limit malloc size when reading/writing file */
#define MAX_BUF                         (8*1024)

unsigned int flag;
const char *progname;
const char commands[] = 
"commands and options:\n\
--get --address=<register address>\n\
--set --address=<register address> --param=<register value>\n\
--set --address=<register address> --or=<Or-ing mask value>\n\
--set --address=<register address> --and=<And-ing mask value>\n\
--read --address=<target address> --length=<bytes> --file=<filename>\n\
--write --address=<target address> [--file=<filename> | --param=<value>] [--uncompress]\n\
--execute --address=<function start address> --param=<input param>\n\
--begin --address=<function start address>\n\
--nvram <segmentname>\n\
--info\n\
--test --address=<target address> --length=<cmd size> --count=<iterations>\n\
--quiet\n\
--done\n\
--timeout=<time to wait for command completion in seconds>\n\
--interface=<ethx for socket interface, sysfs for sysfs interface>\n\
--device=<device name>\n\
The options can also be given in the abbreviated form --option=x or -o x. The options can be given in any order";

#define A_ROUND_UP(x, y)             ((((x) + ((y) - 1)) / (y)) * (y))

#define quiet() (flag & QUIET_FLAG)
#define nqprintf(args...) if (!quiet()) {printf(args);}
#define min(x,y) ((x) < (y) ? (x) : (y))

INLINE void *
MALLOC(int nbytes)
{
    void *p= malloc(nbytes);

    if (!p)
    {
        err(1, "Cannot allocate memory\n");
    }

    return p;
}

static void
usage(void)
{
    fprintf(stderr, "usage:\n%s [-i device] commands\n", progname);
    fprintf(stderr, "%s\n", commands);
    exit(-1);
}

/* Get Target Type and Version information. */
static void
BMIGetTargetInfo(int dev, struct bmi_target_info *targ_info)
{
    int nbyte;

    struct bmi_get_target_info_cmd bmicmd;
    A_UINT32 sentinal;

    nqprintf("BMIGetTargetInfo\n");
    bmicmd.command = BMI_GET_TARGET_INFO;
    nbyte = write(dev, &bmicmd, sizeof(bmicmd));
    if (nbyte != sizeof(bmicmd))
        err(1, "BMIGetTargetInfo cannot send cmd (%d)\n", nbyte);

    /* Verify expected sentinal in target's response */
    nbyte = read(dev, &sentinal, sizeof(sentinal));
    if (nbyte != sizeof(sentinal))
        err(1, "BMIGetTargetInfo cannot read sentinal (%d)\n", nbyte);

    if (sentinal != TARGET_VERSION_SENTINAL)
        err(1, "BMIGetTargetInfo incorrect sentinal (0x%x)\n", sentinal);

    /* Read target information */
    nbyte = read(dev, targ_info, sizeof(*targ_info));
    if (nbyte != sizeof(*targ_info))
        err(1, "BMIGetTargetInfo cannot read Target Info (%d)\n", nbyte);

    /* Validate byte count in target's response */
    if (targ_info->target_info_byte_count != sizeof(*targ_info))
        err(1, "BMIGetTargetInfo unexpected byte count (%d)\n", targ_info->target_info_byte_count);
}

static void
BMIDone(int dev)
{
    int nbyte;

    struct bmi_done_cmd bmicmd;

    nqprintf("BMI Done\n");
    bmicmd.command = BMI_DONE;
    nbyte = write(dev, &bmicmd, sizeof(bmicmd));
    if (nbyte != sizeof(bmicmd))
        err(1, "BMIDone cannot send cmd (%d)\n", nbyte);
}

static void
BMIReadMemory(int dev, A_UINT32 address, char *buffer, A_UINT32 length)
{
    int nbyte;
    unsigned int remaining, rxlen;
    struct bmi_read_memory_cmd bmicmd;

    nqprintf("BMI Read Memory (address: 0x%x, length: %d)\n",
        address, length);

    for (remaining = length; remaining; remaining -= rxlen) {
        rxlen = min(remaining, BMI_DATASZ_MAX);
        bmicmd.command = BMI_READ_MEMORY;
        bmicmd.address = address;
        bmicmd.length = rxlen;

        nbyte = write(dev, &bmicmd, sizeof(bmicmd));
        if (nbyte != sizeof(bmicmd))
            err(1, "BMIReadMemory cannot send cmd (%d)\n", nbyte);

        nbyte = read(dev, buffer, rxlen);
        if (nbyte != rxlen)
            err(1, "BMIReadMemory cannot read response (%d)\n", nbyte);
        buffer += rxlen;
            address += rxlen;
    }
}

static void
BMIWriteMemory(int dev, A_UINT32 address, char *buffer, A_UINT32 length)
{
    int nbyte;
    unsigned int remaining, txlen;
    struct {
        struct bmi_write_memory_cmd_hdr bmi_write_cmd;
        char data[BMI_DATASZ_MAX];
    } bmicmd;

    nqprintf("BMI Write Memory (address: 0x%x, length %d)\n",
        address, length);

    for (remaining = length; remaining; remaining -= txlen) {
        int bmi_write_len;

        txlen = min(remaining, (BMI_DATASZ_MAX));
        bmicmd.bmi_write_cmd.command = BMI_WRITE_MEMORY;
        bmicmd.bmi_write_cmd.address = address;
        bmicmd.bmi_write_cmd.length = txlen;
        memcpy(bmicmd.data, &buffer[length-remaining], txlen);

        bmi_write_len = txlen + sizeof(bmicmd.bmi_write_cmd);
        nbyte = write(dev, &bmicmd, bmi_write_len);
        if (nbyte != bmi_write_len)
            err(1, "BMIWriteMemory cannot send cmd (%d)\n", nbyte);

       address += txlen;
	   printk("write bin file\n");
    }
}

static void
BMILZStreamStart(int dev, A_UINT32 address)
{
    int nbyte;
    struct bmi_lz_stream_start_cmd bmicmd;

    nqprintf("BMI LZ Stream Start (address: 0x%x)\n", address);

    bmicmd.command = BMI_LZ_STREAM_START;
    bmicmd.address = address;

    nbyte = write(dev, &bmicmd, sizeof(bmicmd));
    if (nbyte != sizeof(bmicmd))
        err(1, "BMILZStreamStart cannot send cmd (%d)\n", nbyte);
}

static void
BMILZData(int dev, char *buffer, A_UINT32 length)
{
    int nbyte;
    unsigned int remaining, txlen;
    struct {
        struct bmi_lz_data_cmd_hdr bmi_lz_data_cmd;
        char data[BMI_DATASZ_MAX];
    } bmicmd;

    nqprintf("BMI LZ Data (length: 0x%x)\n", length);

    for (remaining = length; remaining; remaining -= txlen) {
        int bmi_write_len;

        txlen = min(remaining, (BMI_DATASZ_MAX));
        bmicmd.bmi_lz_data_cmd.command = BMI_LZ_DATA;
        bmicmd.bmi_lz_data_cmd.length = txlen;
        memcpy(bmicmd.data, &buffer[length-remaining], txlen);

        bmi_write_len = txlen + sizeof(bmicmd.bmi_lz_data_cmd);
        nbyte = write(dev, &bmicmd, bmi_write_len);
        if (nbyte != bmi_write_len)
            err(1, "BMILZData cannot send cmd (%d)\n", nbyte);
    }
}

static void
BMIReadSOCRegister(int dev, A_UINT32 address, A_UINT32 *value)
{
    int nbyte;
    struct bmi_read_soc_register_cmd bmicmd;

    nqprintf("BMI Read SOC Register (address: 0x%x)\n", address);
    bmicmd.command = BMI_READ_SOC_REGISTER;
    bmicmd.address = address;
    nbyte = write(dev, &bmicmd, sizeof(bmicmd));
    if (nbyte != sizeof(bmicmd))
        err(1, "BMIReadSOCRegister cannot send cmd(%d)\n", nbyte);

    nbyte = read(dev, value, sizeof(*value));
    if (nbyte != sizeof(*value))
        err(1, "BMIReadSOCRegister cannot read response (%d)\n", nbyte);
}

static void
BMIWriteSOCRegister(int dev, A_UINT32 address, A_UINT32 param)
{
    int nbyte;
    struct bmi_write_soc_register_cmd bmicmd;

    nqprintf("BMI Write SOC Register (address: 0x%x, param: 0x%x)\n",
        address, param);

    bmicmd.command = BMI_WRITE_SOC_REGISTER;
    bmicmd.address = address;
    bmicmd.value = param;
    nbyte = write(dev, &bmicmd, sizeof(bmicmd));
    if (nbyte != sizeof(bmicmd))
        err(1, "BMIWriteSOCRegister cannot send cmd(%d)\n", nbyte);
}

static void
BMISetAppStart(int dev, A_UINT32 address)
{
    int nbyte;
    struct bmi_set_app_start_cmd bmicmd;
    nqprintf("BMI Set App Start (address: 0x%x)\n", address);

    bmicmd.command = BMI_SET_APP_START;
    bmicmd.address = address;
    nbyte = write(dev, &bmicmd, sizeof(bmicmd));
    if (nbyte != sizeof(bmicmd))
        err(1, "BMISetAppStart cannot send cmd(%d)\n", nbyte);
}

static A_UINT32
BMIExecute(int dev, A_UINT32 address, A_UINT32 param)
{
    int nbyte;
    A_UINT32 rv;
    struct bmi_execute_cmd bmicmd;

    nqprintf("BMI Execute (address: 0x%x, param: 0x%x)\n", address, param);

    bmicmd.command = BMI_EXECUTE;
    bmicmd.address = address;
    bmicmd.param = param;
    nbyte = write(dev, &bmicmd, sizeof(bmicmd));
    if (nbyte != sizeof(bmicmd))
        err(1, "BMIExecute cannot send cmd(%d)\n", nbyte);

    do {
        nbyte = read(dev, &rv, sizeof(rv));
    } while ((nbyte < 0) && (errno == ETIMEDOUT));

    if (nbyte != sizeof(rv))
        err(1, "BMIExecute cannot read response (%d)\n", nbyte);

    return rv;
}

int
main (int argc, char **argv) {
    int c, s, fd, dev;
    unsigned int address, length;
    unsigned int count, param;
    char filename[PATH_MAX], ifname[IFNAMSIZ];
    char nvramname[BMI_NVRAM_SEG_NAME_SZ];
    char devicename[PATH_MAX];
    unsigned int cmd;
    struct ifreq ifr;
    char *buffer;
    struct stat filestat;
    int target_version = -1;
    int target_type = -1;
    int ifname_set = 0;
    unsigned int bitwise_mask;
    unsigned int timeout;
    char *ethIf;
    
    progname = argv[0];
    if (argc == 1) usage();

    flag = 0;
    memset(filename, '\0', sizeof(filename));
    memset(ifname, '\0', IFNAMSIZ);

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"get", 0, NULL, 'g'},
            {"set", 0, NULL, 's'},
            {"read", 0, NULL, 'r'},
            {"test", 0, NULL, 't'},
            {"timeout", 1, NULL, 'T'},
            {"file", 1, NULL, 'f'},
            {"done", 0, NULL, 'd'},
            {"write", 0, NULL, 'w'},
            {"begin", 0, NULL, 'b'},
            {"count", 1, NULL, 'c'},
            {"param", 1, NULL, 'p'},
            {"length", 1, NULL, 'l'},
            {"execute", 0, NULL, 'e'},
            {"address", 1, NULL, 'a'},
            {"interface", 1, NULL, 'i'},
            {"info", 0, NULL, 'I'},
            {"and", 1, NULL, 'n'},
            {"or", 1, NULL, 'o'},
            {"nvram", 1, NULL, 'N'},
            {"quiet", 0, NULL, 'q'},
            {"uncompress", 0, NULL, 'u'},
            {"device", 1, NULL, 'D'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "rwtebdgsIqf:l:a:p:i:c:n:o:m:D:",
                         long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'r':
            cmd = BMI_READ_MEMORY;
            break;

        case 'w':
            cmd = BMI_WRITE_MEMORY;
            break;

        case 'e':
            cmd = BMI_EXECUTE;
            break;

        case 'b':
            cmd = BMI_SET_APP_START;
            break;

        case 'd':
            cmd = BMI_DONE;
            break;

        case 'g':
            cmd = BMI_READ_SOC_REGISTER;
            break;

        case 's':
            cmd = BMI_WRITE_SOC_REGISTER;
            break;

        case 't':
            cmd = BMI_TEST;
            break;

        case 'f':
            memset(filename, '\0', sizeof(filename));
            strncpy(filename, optarg, sizeof(filename));
            flag |= FILE_FLAG;
            break;

        case 'l':
            length = atoi(optarg);
            flag |= LENGTH_FLAG;
            break;

        case 'a':
            address = strtoul(optarg, NULL, 0);
            flag |= ADDRESS_FLAG;
            break;

        case 'p':
            param = strtoul(optarg, NULL, 0);
            flag |= PARAM_FLAG;
            break;

        case 'c':
            count = atoi(optarg);
            flag |= COUNT_FLAG;
            break;

        case 'i':
            memset(ifname, '\0', 8);
            strcpy(ifname, optarg);
            ifname_set = 1;
            break;

        case 'I':
            cmd = BMI_GET_TARGET_INFO;
            break;
            
        case 'n':
            flag |= PARAM_FLAG | AND_OP_FLAG | BITWISE_OP_FLAG;
            bitwise_mask = strtoul(optarg, NULL, 0);
            break;
            
        case 'o':                
            flag |= PARAM_FLAG | BITWISE_OP_FLAG;
            bitwise_mask = strtoul(optarg, NULL, 0);
            break;

        case 'q':
            flag |= QUIET_FLAG;
            break;
            
        case 'u':
            flag |= UNCOMPRESS_FLAG;
            break;

        case 'T':
            timeout = strtoul(optarg, NULL, 0);
            timeout = timeout * 10; // convert seconds to 100ms units
            flag |= TIMEOUT_FLAG;
            break;
            
        case 'D':
            strncpy(devicename, optarg, sizeof(devicename)-5);
            strcat(devicename, "/bmi");
            flag |= DEVICE_FLAG;
            break;

        case 'N':
            cmd = BMI_NVRAM_PROCESS;
            memset(nvramname, '\0', sizeof(nvramname));
            strncpy(nvramname, optarg, sizeof(nvramname));
            break;

        default:
            usage();
        }
    }

    if ((strncmp(ifname, "sysfs", 5)) == 0) {
        flag |= BMI_SYSFS_FLAG;
    }

    if (flag & BMI_SYSFS_FLAG) {
        /* BMI needs to use the sysfs interface */
        if (!(flag & DEVICE_FLAG)) {
            err(1, "Must specify a device name (--device)");
        }

        if ((dev = open(devicename, O_RDWR)) < 0) {
            err(1, "Failed (%d) to open bmi file, %s\n", dev, devicename);
        }
    } else {
        /* Legacy interface */
        if (!ifname_set) {
            if ((ethIf = getenv("NETIF")) == NULL) {
                ethIf = "eth1";
            }
            strcpy(ifname, ethIf);
        }
        s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) err(1, "socket");
    
        strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
        /* Verify that the Target is alive.  If not, wait for it. */
        {
            int rv;
            static int waiting_msg_printed = 0;
    
            buffer = (char *)MALLOC(sizeof(struct bmi_target_info));
            ((int *)buffer)[0] = AR6000_XIOCTL_TARGET_INFO;
            ifr.ifr_data = buffer;
            while ((rv=ioctl(s, AR6000_IOCTL_EXTENDED, &ifr)) < 0)
            {
                if (errno == ENODEV) {
                    /* 
                     * Give the Target device a chance to start.
                     * Then loop back and see if it's alive till the specified
                     * timeout
                     */
                    if (flag & TIMEOUT_FLAG) {
                        if (!timeout) {
                            err(1, "%s", ifr.ifr_name);
                            exit(1);
                        }
                        timeout--;
                    }
                    if (!waiting_msg_printed) {
                        nqprintf("bmiloader is waiting for Target....\n");
                        waiting_msg_printed = 1;
                    }
                    usleep(100000); /* Wait for 100ms */
                } else {
                    printf("Unexpected error on AR6000_XIOCTL_TARGET_INFO: %d\n", rv);
                    exit(1);
                }
            }
            target_version = ((int *)buffer)[0];
            target_type = ((int *)buffer)[1];
            free(buffer);
        }
    }

    switch(cmd)
    {
    case BMI_DONE:
        nqprintf("BMI Done\n");
        if (flag & BMI_SYSFS_FLAG) {
            BMIDone(dev);
        } else {
            buffer = (char *)MALLOC(4);
            ((int *)buffer)[0] = AR6000_XIOCTL_BMI_DONE;
            ifr.ifr_data = buffer;
            if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
            {
                err(1, "%s", ifr.ifr_name);
            }
            free(buffer);
        }
        break;

    case BMI_TEST:
        if ((flag & (COUNT_FLAG | LENGTH_FLAG | ADDRESS_FLAG)) == 
            (COUNT_FLAG | LENGTH_FLAG | ADDRESS_FLAG))
        {
            nqprintf("BMI Test (address: 0x%x, length: %d, count: %d)\n", 
                    address, length, count);
            if (flag & BMI_SYSFS_FLAG) {
                err(1, "Cmd: %d not supported via sysfs interface", cmd);
            } else {
                buffer = (char *)MALLOC(16);
                ((int *)buffer)[0] = AR6000_XIOCTL_BMI_TEST;
                ((int *)buffer)[1] = address;
                ((int *)buffer)[2] = length;
                ((int *)buffer)[3] = count;
                ifr.ifr_data = buffer;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                }
                free(buffer);
            }
        }
        else usage();
        break;

    case BMI_READ_MEMORY:
        if ((flag & (ADDRESS_FLAG | LENGTH_FLAG | FILE_FLAG)) == 
            (ADDRESS_FLAG | LENGTH_FLAG | FILE_FLAG))
        {
            nqprintf(
                 "BMI Read Memory (address: 0x%x, length: %d, filename: %s)\n",
                  address, length, filename);

            if ((fd = open(filename, O_CREAT|O_WRONLY|O_TRUNC, 00644)) < 0)
            {
                perror("Could not create a file");
                exit(1);
            }
            buffer = (char *)MALLOC(MAX_BUF + 12);

            {
                unsigned int remaining = length;

                while (remaining)
                {
                    length = (remaining > MAX_BUF) ? MAX_BUF : remaining;

                    if (flag & BMI_SYSFS_FLAG) {
                        BMIReadMemory(dev, address, buffer, length);
                    } else {
                        ((int *)buffer)[0] = AR6000_XIOCTL_BMI_READ_MEMORY;
                        ((int *)buffer)[1] = address;
    
                        /*
                         * We round up the requested length because some
                         * SDIO Host controllers can't handle other lengths;
                         * but we still only write the requested number of
                         * bytes to the file.
                         */
                        ((int *)buffer)[2] = A_ROUND_UP(length, 4);
                        ifr.ifr_data = buffer;
                        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                        {
                            err(1, "%s", ifr.ifr_name);
                        }
                    }

                    write(fd, buffer, length);
                    remaining -= length;
                    address += length;
                }
            }

            close(fd);
            free(buffer);
        }
        else usage();
        break;

    case BMI_WRITE_MEMORY:
        if (!(flag & ADDRESS_FLAG))
        {
            if (flag & FILE_FLAG) {
                /*
                 * When we fail to specify an address for a file write,
                 * assume it's a segmented file which includes metadata
                 * for addresses and lengths for each segment.
                 */
                address = BMI_SEGMENTED_WRITE_ADDR;
                flag |= ADDRESS_FLAG;
            } else {
                usage(); /* no address specified */
            }
        }
        if (!(flag & (FILE_FLAG | PARAM_FLAG)))
        {
            usage(); /* no data specified */
        }
        if ((flag & FILE_FLAG) && (flag & PARAM_FLAG))
        {
            usage(); /* too much data specified */
        }
        if ((flag & UNCOMPRESS_FLAG) && !(flag & FILE_FLAG))
        {
            usage(); /* uncompress only works with a file */
        }

        if (flag & FILE_FLAG)
        {
            nqprintf(
                 "BMI Write %sMemory (address: 0x%x, filename: %s)\n",
                  ((flag & UNCOMPRESS_FLAG) ? "compressed " : ""),
                  address, filename);
            if ((fd = open(filename, O_RDONLY)) < 0)
            {
                perror("Could not open file");
                exit(1);
            }
            memset(&filestat, '\0', sizeof(struct stat));
            buffer = (char *)MALLOC(MAX_BUF + 12);
            fstat(fd, &filestat);
            length = filestat.st_size;

            if (flag & UNCOMPRESS_FLAG) {
                /* Initiate compressed stream */
                if (flag & BMI_SYSFS_FLAG) {
                    BMILZStreamStart(dev, address);
                } else {
                    ((int *)buffer)[0] = AR6000_XIOCTL_BMI_LZ_STREAM_START;
                    ((int *)buffer)[1] = address;
                    ifr.ifr_data = buffer;
                    if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                    {
                        err(1, "%s", ifr.ifr_name);
                    }
                }
            }
        }
        else
        { /* PARAM_FLAG */
            nqprintf(
                 "BMI Write Memory (address: 0x%x, value: 0x%x)\n",
                  address, param);
            length = sizeof(param);
            if (flag & BMI_SYSFS_FLAG) {
                buffer = (char *)&param;
            } else {
                buffer = (char *)MALLOC(length + 12);
                *(unsigned int *)(&buffer[12]) = param;
            }
            fd = -1;
        }

        /*
         * Write length bytes of data to memory.
         * Data is either present in buffer OR
         * needs to be read from fd in MAX_BUF chunks.
         *
         * Within the kernel, the implementation of
         * AR6000_XIOCTL_BMI_WRITE_MEMORY further
         * limits the size of each transfer over the
         * interconnect according to BMI protocol
         * limitations.
         */ 
        {
            unsigned int remaining = length;
            int *pLength;

            while (remaining)
            {
                int nbyte;

                length = (remaining > MAX_BUF) ? MAX_BUF : remaining;
                if (flag & UNCOMPRESS_FLAG) {
                    /* 0 pad last word of data to avoid messy uncompression */
                    ((A_UINT32 *)buffer)[2+((length-1)/4)] = 0;

                    if (flag & BMI_SYSFS_FLAG) {
                        nbyte = read(fd, buffer, length);
                        if (nbyte != length) {
                            err(1, "Read from compressed file failed (%d)\n", nbyte);
                        }
                        BMILZData(dev, buffer, length);
                    } else {
                        if (read(fd, &buffer[8], length) != length)
                        {
                            perror("read from compressed file failed");
                            exit(1);
                        }
                        ((int *)buffer)[0] = AR6000_XIOCTL_BMI_LZ_DATA;
                        pLength = &((int *)buffer)[1];
                    }
                } else {
                    if (fd > 0)
                    {
                        if (flag & BMI_SYSFS_FLAG) {
                            nbyte = read(fd, buffer, length);
                            if (nbyte != length) {
                                err(1, "Read from file failed (%d)\n", nbyte);
                            }
                        } else {
                            if (read(fd, &buffer[12], length) != length)
                            {
                                perror("read from file failed");
                                exit(1);
                            }
                        }
                    }

                    if (flag & BMI_SYSFS_FLAG) {
                        BMIWriteMemory(dev, address, buffer, length);
                    } else {
                        ((int *)buffer)[0] = AR6000_XIOCTL_BMI_WRITE_MEMORY;
                        ((int *)buffer)[1] = address;
                        pLength = &((int *)buffer)[2];
                    }
                }

                /*
                 * We round up the requested length because some
                 * SDIO Host controllers can't handle other lengths.
                 * This generally isn't a problem for users, but it's
                 * something to be aware of.
                 */
                if (!(flag & BMI_SYSFS_FLAG)) {
                    *pLength = A_ROUND_UP(length, 4);
                    ifr.ifr_data = buffer;
                    while (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                    {
                        err(1, "%s", ifr.ifr_name);
                    }
                }

                remaining -= length;
                address += length;
            }
        }

        if (flag & BMI_SYSFS_FLAG) {
            if (flag & FILE_FLAG) {
                free(buffer);
                close(fd);
            }

            if (flag & UNCOMPRESS_FLAG) {
                BMILZStreamStart(dev, 0);
            }
        } else {
            if (fd > 0)
            {
                close(fd);
                if (flag & UNCOMPRESS_FLAG) {
                    /*
                     * Close compressed stream and open a new (fake)
                     * one.  This serves mainly to flush Target caches.
                     */
                    ((int *)buffer)[0] = AR6000_XIOCTL_BMI_LZ_STREAM_START;
                    ((int *)buffer)[1] = 0;
                    ifr.ifr_data = buffer;
                    if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                    {
                        err(1, "%s", ifr.ifr_name);
                    }
                }
            }
            free(buffer);
        }

        break;

    case BMI_READ_SOC_REGISTER:
        if ((flag & (ADDRESS_FLAG)) == (ADDRESS_FLAG))
        {
            nqprintf("BMI Read Register (address: 0x%x)\n", address);
            if (flag & BMI_SYSFS_FLAG) {
                BMIReadSOCRegister(dev, address, &param);
            } else {
                buffer = (char *)MALLOC(8);
                ((int *)buffer)[0] = AR6000_XIOCTL_BMI_READ_SOC_REGISTER;
                ((int *)buffer)[1] = address;
                ifr.ifr_data = buffer;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                }
                param = ((int *)buffer)[0];
                free(buffer);
            }

            if (quiet()) {
                printf("0x%x\n", param);
            } else {
                printf("Return Value from target: 0x%x\n", param);
            }
        }
        else usage();
        break;

    case BMI_WRITE_SOC_REGISTER:
        if ((flag & (ADDRESS_FLAG | PARAM_FLAG)) == (ADDRESS_FLAG | PARAM_FLAG))
        {
            A_UINT32 origvalue = 0;
            
            if (flag & BITWISE_OP_FLAG) {
                /* first read */    
                if (flag & BMI_SYSFS_FLAG) {
                    BMIReadSOCRegister(dev, address, &origvalue);
                    param = origvalue;
                } else {
                    buffer = (char *)MALLOC(8);
                    ((int *)buffer)[0] = AR6000_XIOCTL_BMI_READ_SOC_REGISTER;
                    ((int *)buffer)[1] = address;
                    ifr.ifr_data = buffer;
                    if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                    {
                        err(1, "%s", ifr.ifr_name);
                    }
                    param = ((int *)buffer)[0];
                    origvalue = param;
                    free(buffer);
                }
                
                /* now modify */
                if (flag & AND_OP_FLAG) {
                    param &= bitwise_mask;        
                } else {
                    param |= bitwise_mask;
                }               
            
                /* fall through to write out the parameter */
            }
            
            if (flag & BITWISE_OP_FLAG) {
                if (quiet()) {
                    printf("0x%x\n", origvalue);
                } else {
                    printf("BMI Bit-Wise (%s) modify Register (address: 0x%x, orig:0x%x, new: 0x%x,  mask:0x%X)\n", 
                       (flag & AND_OP_FLAG) ? "AND" : "OR", address, origvalue, param, bitwise_mask );   
                }
            } else{ 
                nqprintf("BMI Write Register (address: 0x%x, param: 0x%x)\n", address, param);
            }
            
            if (flag & BMI_SYSFS_FLAG) {
                BMIWriteSOCRegister(dev, address, param);
            } else {
                buffer = (char *)MALLOC(12);
                ((int *)buffer)[0] = AR6000_XIOCTL_BMI_WRITE_SOC_REGISTER;
                ((int *)buffer)[1] = address;
                ((int *)buffer)[2] = param;
                ifr.ifr_data = buffer;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                }
                free(buffer);
            }
        }
        else usage();
        break;

    case BMI_EXECUTE:
        if ((flag & (ADDRESS_FLAG | PARAM_FLAG)) == (ADDRESS_FLAG | PARAM_FLAG))
        {
            nqprintf("BMI Execute (address: 0x%x, param: 0x%x)\n", address, param);
            if (flag & BMI_SYSFS_FLAG) {
                A_UINT32 rv;

                rv = BMIExecute(dev, address, param);
                param = rv;
            } else {
                buffer = (char *)MALLOC(12);
                ((int *)buffer)[0] = AR6000_XIOCTL_BMI_EXECUTE;
                ((int *)buffer)[1] = address;
                ((int *)buffer)[2] = param;
                ifr.ifr_data = buffer;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                }
                param = ((int *)buffer)[0];
                free(buffer);
            }

            if (quiet()) {
                printf("0x%x\n", param);
            } else {
                printf("Return Value from target: 0x%x\n", param);
            }
        }
        else usage();
        break;

    case BMI_SET_APP_START:
        if ((flag & ADDRESS_FLAG) == ADDRESS_FLAG)
        {
            nqprintf("BMI Set App Start (address: 0x%x)\n", address);
            if (flag & BMI_SYSFS_FLAG) {
                BMISetAppStart(dev, address);
            } else {
                buffer = (char *)MALLOC(8);
                ((int *)buffer)[0] = AR6000_XIOCTL_BMI_SET_APP_START;
                ((int *)buffer)[1] = address;
                ifr.ifr_data = buffer;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                }
                free(buffer);
            }
        }
        else usage();
        break;
    case BMI_GET_TARGET_INFO:
        nqprintf("BMI Target Info:\n");

        if (flag & BMI_SYSFS_FLAG) {
            struct bmi_target_info targ_info;

            BMIGetTargetInfo(dev, &targ_info);
            target_version = targ_info.target_ver;
            target_type = targ_info.target_type;
        }

        printf("TARGET_TYPE=%s\n",
                (target_type == TARGET_TYPE_AR6001) ? "AR6001" :
                ((target_type == TARGET_TYPE_AR6002) ? "AR6002" : 
                ((target_type == TARGET_TYPE_AR6003) ? "AR6003" :
                ((target_type == TARGET_TYPE_MCKINLEY) ? "MCKINLEY" : "unknown"))));
        printf("TARGET_VERSION=0x%x\n", target_version);
        break;
    case BMI_NVRAM_PROCESS:
    {
        int rv;

        buffer = (char *)MALLOC(sizeof(cmd) + sizeof(nvramname));
        ((int *)buffer)[0] = AR6000_XIOCTL_BMI_NVRAM_PROCESS;
        memcpy(&buffer[4], nvramname, sizeof(nvramname));
        
        ifr.ifr_data = buffer;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        rv = ((int *)buffer)[0];
        free(buffer);

        if (quiet()) {
            printf("0x%x\n", rv);
        } else {
            printf("Return Value from target: 0x%x\n", rv);
        }
        break;
    }

    default:
        usage();
    }

    exit (0);
}
