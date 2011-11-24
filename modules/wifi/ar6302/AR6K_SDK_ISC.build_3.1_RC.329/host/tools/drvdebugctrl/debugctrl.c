
/*
 * Copyright (c) 2009 Atheros Communications Inc.
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
#include <err.h>

#include <a_config.h>
#include <a_osapi.h>
#include <a_types.h>
#include <athdrv_linux.h>

const char *prog_name;
struct ifreq ifr;
int sock;

char module_name[128];

const char cmd_args[] =
"arguments:\n\
  --module=   , -m <module name>    name of module (htc,bmi,wmi,wlan) \n\
  --set=      , -s <bit map>        set the current debug mask  \n\
  --setbit=   , -b <bit number>     set a single bit (0..31)    \n\
  --clrbit=   , -c <bit number>     clear a single bit (0..31)  \n\
  --dumpinfo  , -d                  dump info for this module   \n\
  --dumpall                         dump all registered module debug info \n\
  --interface=, -i <netif name>     (e.g. eth1) \n\
\n";

void
usage(void)
{
    fprintf(stderr, "usage:\n%s arguments...\n", prog_name);
    fprintf(stderr, "%s\n", cmd_args);
    exit(1);
}

void
do_ioctl(void *ioctl_data)
{
    ifr.ifr_data = (char *)ioctl_data;

    if (ioctl(sock, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        err(1, "%s", ifr.ifr_name);
        exit (1);
    }
}

void DumpModuleDebugInfo(char *name)
{
    struct  debuginfocmd {
        int    cmd;
        struct drv_debug_module_s modinfo; 
    } command;

    memset(&command,0,sizeof(command));
    
    command.cmd = AR6000_XIOCTL_DUMP_MODULE_DEBUG_INFO;
    strncpy(command.modinfo.modulename,name,sizeof(command.modinfo.modulename)); 
    printf("Dumping module : %s info.... \n",name);
    do_ioctl(&command);
}

void SetDebugBitMask(char *name,unsigned int mask)
{
    struct  debuginfocmd {
        int    cmd;
        struct drv_debug_module_s modinfo; 
    } command;

    memset(&command,0,sizeof(command));
    
    command.cmd = AR6000_XIOCTL_MODULE_DEBUG_SET_MASK;
    command.modinfo.mask = mask;
    strncpy(command.modinfo.modulename,name,sizeof(command.modinfo.modulename)); 
    printf("Setting module : %s mask to: 0x%X \n", module_name, mask);
    do_ioctl(&command);
}

unsigned int GetDebugBitMask(char *name)
{
    struct  debuginfocmd {
        int    cmd;
        struct drv_debug_module_s modinfo; 
    } command;

    memset(&command,0,sizeof(command));
    
    command.cmd = AR6000_XIOCTL_MODULE_DEBUG_GET_MASK;
    strncpy(command.modinfo.modulename,name,sizeof(command.modinfo.modulename)); 
    do_ioctl(&command);    
    printf("Got module : %s mask : 0x%X \n", module_name, command.modinfo.mask);
    return command.modinfo.mask;
}

void SetDebugBitNo(char *name,unsigned int bit)
{
    unsigned int mask = GetDebugBitMask(name);
    
    mask |= (1 << bit);
    SetDebugBitMask(name, mask);
}

void ClearDebugBitNo(char *name,unsigned int bit)
{
    unsigned int mask = GetDebugBitMask(name);
    
    mask &= ~(1 << bit);
    SetDebugBitMask(name, mask);
}

typedef enum {
    MASK_ACTION_NONE = 0,
    MASK_ACTION_SET_MASK,
    MASK_ACTION_SET_BIT,
    MASK_ACTION_CLR_BIT,
    MASK_ACTION_DUMP_INFO,
    MASK_ACTION_DUMP_ALL_INFO
} MASK_ACTION;

int
main (int argc, char **argv)
{
 
    int c;
    int gotname = 0;
    MASK_ACTION action = MASK_ACTION_NONE;
    unsigned int bitmask;
    unsigned int bitpos;
    
    prog_name = argv[0];

    if (argc == 1) {
        usage();
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        err(1, "socket");
    }

    memset(ifr.ifr_name, '\0', sizeof(ifr.ifr_name));
    strcpy(ifr.ifr_name, "eth1"); /* default */
  
    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"module", 1, NULL, 'm'},
            {"set", 1, NULL, 's'},
            {"setbit", 1, NULL, 'b'},
            {"clrbit", 1, NULL, 'c'},
            {"interface", 1, NULL, 'i'},
            {"dumpall", 0, NULL, 'a'},
            {"dumpinfo", 0, NULL, 'd'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "m:s:b:c:i:d",
                         long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {

        case 'm':
        {
            strncpy(module_name, optarg, sizeof(module_name));
            gotname = 1;
            break;
        }
        case 's':
        {
            bitmask = strtoul(optarg, NULL, 0);
            action = MASK_ACTION_SET_MASK;
            break;
        }

        case 'b':
        {
            bitpos = strtoul(optarg, NULL, 0);
            if (bitpos > 31) {
                printf(" bit number is too high");
                break;    
            }
            action = MASK_ACTION_SET_BIT;
            break;
        }

        case 'c':
        {
            bitpos = strtoul(optarg, NULL, 0);
            if (bitpos > 31) {
                printf(" bit number is too high");
                break;    
            }
            
            action = MASK_ACTION_CLR_BIT;
            break;
        }

        case 'a':
        {
            action = MASK_ACTION_DUMP_ALL_INFO;
            break;
        }
        case 'd':
            action = MASK_ACTION_DUMP_INFO;
            break;
        case 'i':
        {
            memset(ifr.ifr_name, '\0', sizeof(ifr.ifr_name));
            strncpy(ifr.ifr_name, optarg, sizeof(ifr.ifr_name));
            break;
        }

        default:
        {
            usage();
            break;
        }
        }
    }
    
    if (action == MASK_ACTION_NONE) {
        usage();
        exit(1);  
    }
    
    if (action != MASK_ACTION_DUMP_ALL_INFO) {
        if (!gotname) {
            printf("** must specify module name\n");     
            usage();
            exit(1);
        }
    }
    
    switch (action) {
        
        case MASK_ACTION_DUMP_ALL_INFO :
            DumpModuleDebugInfo("all");
            break;    
        case MASK_ACTION_DUMP_INFO:
            DumpModuleDebugInfo(module_name);
            break;
        case MASK_ACTION_CLR_BIT :
            ClearDebugBitNo(module_name,bitpos);
            DumpModuleDebugInfo(module_name);
            break;   
        case MASK_ACTION_SET_BIT :
            SetDebugBitNo(module_name,bitpos);
            DumpModuleDebugInfo(module_name);
            break;    
        case MASK_ACTION_SET_MASK :
            SetDebugBitMask(module_name,bitmask);
            DumpModuleDebugInfo(module_name);
            break;    
        default:
            break;   
    }  
    

    exit(0);
}
