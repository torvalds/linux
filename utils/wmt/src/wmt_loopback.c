/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */
#include "wmt_ioctl.h"
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/uio.h>
//#include <sys/endian.h>
#include <linux/serial.h> /* struct serial_struct  */

#define LEN_MAX 1024

typedef enum {
    LEN_FIXED = 0,
    LEN_INC   = 1,
    LEN_DEC   = 2,
    LEN_RAND  = 3,
    COMBO_POWER_ON  = 4, 
    COMBO_POWER_OFF = 5,
    ADIE_LPK = 6,
    LPBK_OP_MAX
} LPBK_OP_ENUM;

unsigned char WMT_TEST_LPBK_CMD[] = {0x1, 0x2, 0x0, 0x0, 0x7};
unsigned char WMT_TEST_LPBK_EVT[] = {0x2, 0x2, 0x0, 0x0, 0x0};
/*
unsigned char out_buf[2048] = {0};
unsigned char in_buf[2048] = {0};
*/
struct lpbk_package{
	long payload_length;
	unsigned char out_payload[2048];
	unsigned char in_payload[2048];
};

static int wmt_loopback(int type, int count, int max, int delay) {
    ssize_t s_result = 0;
    int wmt_fd;
    int ret = -1;
    int loop = 0;
    int offset;
    unsigned short buf_length = 0;
    unsigned short len_in_cmd;
	struct lpbk_package lpbk_buffer;
    printf("*type(%d) count(%d) max(%d) \n", type, count, max);

    if(type >= LPBK_OP_MAX){
        printf("[%s] cannot support %d opeartion\n", __FUNCTION__, type);
    	return -1;
    }
    
    /* open wmt dev */
    wmt_fd = open("/dev/stpwmt", O_RDWR | O_NOCTTY);
    if (wmt_fd < 0) {
        printf("[%s] Can't open stpwmt \n", __FUNCTION__);
    	return -1;
    }

    if(type == COMBO_POWER_ON){
        printf("[power on combo chip]\n");
        if(ioctl(wmt_fd, WMT_IOCTL_LPBK_POWER_CTRL, 1) != 0)
        {
            printf("[%s] power on combo chip failed\n", __FUNCTION__);
            close(wmt_fd);
            wmt_fd = -1;
            return -1;
        } else {
            close(wmt_fd);
            printf("[power on combo chip ok!]\n");
        }       
        return 0;
    }

    if(type == COMBO_POWER_OFF){
        printf("[power off combo chip]\n");
        if(ioctl(wmt_fd, WMT_IOCTL_LPBK_POWER_CTRL, 0) != 0)
        {
            printf("[%s] power off combo chip failed\n", __FUNCTION__);
            close(wmt_fd);
            ret = -1;
        } else {
            close(wmt_fd);
            printf("[power off combo chip ok!]\n");
        }
        return 0;
    }
    
    /*turn LPBK function on*/
    printf("[power on combo chip]\n");
    if(ioctl(wmt_fd, WMT_IOCTL_LPBK_POWER_CTRL, 1) != 0)
    {
        printf("[%s] Can't power on combo chip ok! failed\n", __FUNCTION__);
        close(wmt_fd);
        wmt_fd = -1;
        return -1;
    } else {
        printf("[power on combo chip ok!]\n");
    }     
		
    /* init length */
    switch (type) {
        case LEN_FIXED:
            buf_length = (unsigned short)max;
            break;
        case LEN_INC:
            buf_length = 1;
            break;
        case LEN_DEC:
            buf_length = max;
            break;
        default:
            /* random */
            break;
    }

    if( (type >= LEN_FIXED) && (type <= LEN_RAND) )
    {
        for (loop = 0; loop < count; loop++) {
            //<1> init buffer
            memset((void *)&lpbk_buffer, 0, sizeof(struct lpbk_package));
            lpbk_buffer.payload_length = buf_length;
            for (offset = 0; offset < buf_length; offset++) {
                lpbk_buffer.out_payload[offset] = (offset + 1)/*for test use: begin from 1*/  & 0xFF;
            }
            
            /*<2> do LPBK*/
            usleep(delay * 1000);   

            if(( ret = ioctl(wmt_fd, WMT_IOCTL_LPBK_TEST, &lpbk_buffer)) != lpbk_buffer.payload_length){
                printf("[%s] LPBK operation failed, return length = %d\n", __FUNCTION__, ret);
                break;
            }
            
            /*<3> compare result*/
            if (memcmp(lpbk_buffer.in_payload, lpbk_buffer.out_payload, lpbk_buffer.payload_length)) {
                printf("[%s] WMT_TEST_LPBK_CMD payload compare error\n", __FUNCTION__);
                break;
            }
            printf("[%s] exec WMT_TEST_LPBK_CMD succeed(loop = %d, size = %ld) \n", __FUNCTION__, loop, lpbk_buffer.payload_length);

            /*<4> update buffer length */
            switch (type) {
            case LEN_INC:
                buf_length = (buf_length == max) ? 1 : buf_length + 1;
                break;
            case LEN_DEC:
                buf_length = (buf_length == 1) ? max : buf_length - 1;
                break;
            case LEN_RAND:
                buf_length = rand() % max + 1;
                break;
            default:
                /* no change */
                break;
            }
        }
    }
    else if( type == ADIE_LPK )
    {
        int adie_chipID = 0;
        for (loop = 0; loop < count; loop++) {
            //<1> init buffer
            memset((void *)&lpbk_buffer, 0, sizeof(struct lpbk_package));
            adie_chipID = 0;
            
            /*<2> do LPBK*/
            usleep(delay * 1000);   

            if(( ret = ioctl(wmt_fd, WMT_IOCTL_ADIE_LPBK_TEST, &lpbk_buffer)) != 2){
                printf("[%s] ADIE_LPK operation failed, return length = %d\n", __FUNCTION__, ret);
                break;
            }
            adie_chipID = ((lpbk_buffer.out_payload[1] >> 4) & 0xf)*1000 + 
                          (lpbk_buffer.out_payload[1] & 0xf)*100 + 
                          ((lpbk_buffer.out_payload[0] >> 4) & 0xf)*10 + 
                          (lpbk_buffer.out_payload[0] & 0xf);
            
            /*<3> compare result*/
            if ( adie_chipID != max ) {
                printf("[%s] ADIE_LPK payload compare error\n", __FUNCTION__);
                break;
            }
            printf("[%s] exec ADIE_LPK succeed(loop = %d, ChipID = %d) \n", __FUNCTION__, 
                loop, adie_chipID);
        }
    }
    
    /*Not to power off chip on default, please manually to do this*/
#if 0    
	/*turn off LPBK function*/
    if(ioctl(wmt_fd, 7, 0) != 0)
    {
    	printf("[%s] turn lpbk function off failed\n", __FUNCTION__);
		ret = -1;
    }
    else
    {
    	ret = 0;
    }
#endif 
    ret = 0;

end:
    if (loop != count) {
        printf("fail at loop(%d) buf_length(%d)\n", loop, buf_length);
    }

    /* close wmt dev */
    if (wmt_fd >= 0) {
        close(wmt_fd);
    }

    return ret;
}
static void print_usage(void)
{
    unsigned int usage_lines = 0;
    static char *(usage[]) = {
        "6620_wmt_lpbk type count length delay",
        "    --type: essential",
        "        -0: loopback test  with fixed packet length",
        "        -1: loopback test with packet length increase 1 per packet based on 1",
        "        -2: loopback test packet length decrease 1 per packet based on 1024",
        "        -3: loopback test with random packet length",
        "        -4: only turn loopback function on without test",
        "        -5: only turn loopback function off without test",
        "        -6: loopback test spi bus by get Adie chpid in SOC chip",
        "    --count: optional, total packet count, 1000 by default",
        "    --length: optional, 1 ~ 1024, 1024 by default",
        "    --delay: optional, interval between packets (ms), 0 by default",

    };
    for (usage_lines = 0 ; usage_lines < sizeof (usage)/sizeof(usage[0]); usage_lines++)
    {
        printf("%s\n", usage[usage_lines]);
    }
}

int main(int argc, char *argv[])
{
    int type = LEN_FIXED;
    int count = 1000;
    int length = 1024;
    int delay = 0;
	if(argc <= 1)
	{
		printf ("Error lack of arguments\n");
		print_usage();
		return -1;
	}
	
    if ((argc > 1) && (argv[1] != NULL)) {
        printf("type: argv[1] %s %d\n", argv[1], atoi(argv[1]));
        type = atoi(argv[1]);
    }

    if ((argc > 2) && (argv[2] != NULL)) {
        printf("count: argv[2] %s %d\n", argv[2], atoi(argv[2]));
        count = atoi(argv[2]);
    }

    if ((argc > 3) && (argv[3] != NULL)) {
        printf("count: argv[3] %s %d\n", argv[3], atoi(argv[3]));
        length = atoi(argv[3]);
        if (0 == length) {
            printf("length is zero, reset default value 1024\n");
            length = 1024;
        }
    }

    if ((argc > 4) && (argv[4] != NULL)) {
        printf("count: argv[4] %s %d\n", argv[4], atoi(argv[4]));
        delay = atoi(argv[4]);
    }
    return wmt_loopback(type, count, length, delay);
}

