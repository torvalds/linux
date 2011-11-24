/*
 * Copyright (c) 2009 Atheros Communications Inc.
 * All rights reserved.

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
#include <sys/time.h>
#include <time.h>
#include <err.h>

#include <a_config.h>
#include <a_osapi.h>
#include <athdefs.h>
#include <a_types.h>
#include "athdrv_linux.h"
#include "pal_api.h"
#include "a_hci.h"

A_INT32
get_input_choice(A_UINT8 *pdu, A_UINT16 *sz)
{
    int choice, fhdl;
    const char *pdu_bin[] = {
                    "hciCmds/HCI_Read_Local_AMP_Info_cmd.bin",
                    "hciCmds/HCI_Read_Local_AMP_ASSOC_cmd.bin",
                    "hciCmds/HCI_Write_Remote_AMP_ASSOC_cmd.bin",
                    "hciCmds/HCI_Create_Physical_Link_cmd.bin",
                    "hciCmds/HCI_Accept_Physical_Link_Req_cmd.bin",
                    "hciCmds/HCI_Create_Logical_Link_Req_cmd.bin",
                    "hciCmds/HCI_Accept_Logical_Link_Req_cmd.bin",
                    "hciCmds/HCI_Disconnect_Logical_Link_Req_cmd.bin",
                    "hciCmds/HCI_Disconnect_Physical_Link_Req_cmd.bin",
                    "hciCmds/HCI_Short_Range_Mode_cmd.bin",
                    "hciCmds/HCI_Read_Link_Quality_cmd.bin",
                    "hciCmds/HCI_Modify_Flow_Spec_cmd.bin",
                    "hciCmds/HCI_Flush_cmd.bin",
                    "hciCmds/HCI_Set_Event_Mask_cmd.bin",
                    "hciCmds/HCI_Set_Event_Mask_Page_2_cmd.bin",
                    "hciCmds/HCI_Read_Data_Block_Size_cmd.bin",
                    };
    const char *other_pdu_bin[] = {
                    "hciCmds/SendDataFrame.bin",
                    "hciCmds/SendDataFrame.bin",
                    "hciCmds/dummy",
                    "hciCmds/dummy",
                    };

    do {
        printf("Choices:\n"
            "0. HCI_Read_Local_AMP_Info\n"
            "1. HCI_Read_Local_AMP_ASSOC\n"
            "2. HCI_Write_Remote_AMP_ASSOC\n"
            "3. HCI_Create_Physical_Link\n"
            "4. HCI_Accept_Physical_Link_Req\n"
            "5. HCI_Create_Logical_Link_Req\n"
            "6. HCI_Accept_Logical_Link_Req\n"
            "7. HCI_Disconnect_Logical_Link_Req\n"
            "8. HCI_Disconnect_Physical_Link_Req\n"
            "9. HCI_Short_Range_Mode_Cmd\n"
            "10. HCI_Read_Link_Quality_Cmd\n"
            "11. HCI_Modify_Flow_Spec_Cmd\n"
            "12. HCI_Flush_Cmd\n"
            "13. HCI_Set_Event_Mask_Cmd\n"
            "14. HCI_Set_Event_Mask_Page_2_Cmd\n"
            "15. HCI_Read_Data_Block_Size_Cmd\n"

            "51. SendDataFrame\n"
            "Select : "
            );
        scanf("%d", &choice);
        if(choice < 51) {   /* HCI cmds */
            fhdl = open((const char *)pdu_bin[choice], O_RDONLY);
        } else {
            fhdl = open((const char *)other_pdu_bin[choice - 51], O_RDONLY);
        }
    } while (fhdl == -1);
    *sz  = read(fhdl, pdu, 1600);

    close(fhdl);

    return choice;
}


int
main(int argc, char **argv)
{
    A_UINT8 pdu[1600];
    A_UINT32 choice;
    A_UINT16  sz, ret;
    void *pdev;
    printf("intf %s\n", argv[1]);
    pdev = pal_init(argv[1]);
    pal_log_cfg(pdev, 0x3);

    memset(pdu, 0, sizeof(pdu));
    while(1)
    {
        choice = get_input_choice(pdu, &sz);

        if(choice < 51) {
            ret = pal_send_hci_cmd(pdev, (char *)pdu, (short)sz);
            if(ret != PAL_HCI_CMD_PROCESSED) {
                printf("CMD IGNORED\n");
            }
        } else {
            if( choice == 51)
                pal_send_acl_data_pkt(pdev, (char *)pdu, (short)sz);
        }
    }
    return 0;
}

