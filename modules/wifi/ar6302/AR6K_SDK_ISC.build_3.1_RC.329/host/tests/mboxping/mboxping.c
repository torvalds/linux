//------------------------------------------------------------------------------
// <copyright file="mboxping.c" company="Atheros">
//    Copyright (c) 2004-2007 Atheros Corporation.  All rights reserved.
// 
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
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netpacket/packet.h>
#ifdef ANDROID
#include <net/if_ether.h>
#define ETH_P_ALL	0x0003
#else
#include <net/ethernet.h>
#endif
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <a_config.h>
#include <a_osapi.h>
#include <a_types.h>
#include <athdrv_linux.h>
#include <epping_test.h>
#include <gmboxif.h>

#define MBOXTX                 0x01
#define MBOXRX                 0x02
#define PRELOAD                0x04
#define PKTSIZE                0x08
#define NUMPKTS                0x10
#define DURATION               0x20

#define MAXWAIT                10
#define MAX_PACKET_LEN         (sizeof(EPPING_HEADER) + 1514)
#define MIN_PACKET_LEN         (sizeof(EPPING_HEADER) + 16)

void catcher(int signo);
void pinger(void);
void finish(int signo);
void tvsub(register struct timeval *out, register struct timeval *in);
void pr_pack(unsigned char *buffer, int length);
void bindnetif(void);
void IndicateTrafficActivity(int StreamID, A_BOOL Active);

void fillpacket(unsigned char *txpacket, int size, A_UINT16 Cmd, A_UINT16 Flags, A_UINT8 *cmdParams, int cmdParamLength);
void sendpacket(unsigned char *txpacket, int size);
void txperffinish(int signo);
void rxperffinish(int signo);
void rxperf_prpack(unsigned char *buffer, int length);

const char *progname;
const char commands[] =
"commands:\n\
--transmit=<mbox> --receive=<mbox> --size=<size> --count=<num> --preload=<num> --duration=<seconds> --verify --quiet \n\
--delay --random  --dumpcreditstates --wait --usepattern --txperf --rxperf --rxburst=<num> \n\
The 'delay' switch will flag each ping packet to trigger the target to delay the packet completion.  The delay \n\
is specified on the target and is usually some ratio of the length of the packet. \n\
The 'random' switch will randomize packets from the minimum packet size to the maximum of 'size' \n\
The 'dumpcreditstates' triggers the HTC layer to dump credit state information to the debugger console \n\
the driver must be compiled with debug prints enabled and the proper debug zone turned on \n\
The 'usepattern' switch forces test to use a known pattern in 'verify' mode \n\
The options can also be given in the abbreviated form --option=x or -o x. The options can be given in any order";
int sockfd;
pid_t ident = 0;
unsigned int numTransmitted = 0;
unsigned int numReceived = 0;
unsigned int numTxPerfTransmitted = 0;
struct sockaddr_ll my_addr;
unsigned int utmin = 999999999;
unsigned int utmax = 0;
unsigned int utsum = 0;
unsigned int numPkts = 0;
unsigned int duration = 10;
unsigned int mboxRx, mboxTx, g_Size, preload = 0;
unsigned int rxperfburstcnt = 8;

#define TRUE 1
#define FALSE 0
int verifyMode = FALSE;
int crcErrors = 0;
int quiet = FALSE;
int dumpdata = FALSE;
int flagDelay = FALSE;
int randomLength = FALSE;
int dumpCreditState = FALSE;
int waitresources = FALSE;
int verfRandomData = TRUE;
int flagtxperf = FALSE;
int flagrxperf = FALSE;
int txperfdone = FALSE;
int rxperfdone = FALSE;

static void
usage(void)
{
    fprintf(stderr, "usage:\n%s [-i device] commands\n", progname);
    fprintf(stderr, "%s\n", commands);
    exit(-1);
}


void NotifyError(char *description)
{  
    int errno_save = errno;
    printf("** Error stream test, send stream: %d loopback on: %d  reason: ",mboxTx, mboxRx);
    errno = errno_save;
    perror(description);
    printf("\n");
}


unsigned char ifname[IFNAMSIZ];

static inline int GetLength(void)
{
    if (randomLength) {
        float value;
            /* scale the size */
        value = ((float)g_Size * (float)rand()) / (float)RAND_MAX;
        return ((int)value < MIN_PACKET_LEN) ? MIN_PACKET_LEN : (int)value;
    }         
    
    return g_Size;
}


int
main (int argc, char **argv) {
    int options, c, i;
    unsigned char rxpacket[MAX_PACKET_LEN];
    unsigned int length;

    progname = argv[0];
    if (argc == 1) usage();

    printf("\n\n");
    memset(ifname, '\0', IFNAMSIZ);
    strcpy((char *)ifname, "eth1");

    while(1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"transmit", required_argument, NULL, 't'},
            {"receive", required_argument, NULL, 'r'},
            {"size", required_argument, NULL, 's'},
            {"count", required_argument, NULL, 'c'},
            {"preload", required_argument, NULL, 'p'},
            {"interface", required_argument, NULL, 'i'},
            {"duration", required_argument, NULL, 'd'},
            {"verify", no_argument, NULL, 'v'},
            {"quiet", no_argument, NULL, 'q'},
            {"bufferdump", no_argument, NULL, 'b'},
            {"delay", no_argument, &flagDelay, TRUE},
            {"random", no_argument, &randomLength, TRUE},
            {"dumpcreditstates",no_argument,&dumpCreditState,TRUE},
            {"wait", no_argument, NULL, 'w'},
            {"usepattern",no_argument,&verfRandomData,FALSE},
            {"txperf", no_argument, &flagtxperf, TRUE},
            {"rxperf", no_argument, &flagrxperf, TRUE},
            {"rxburst", required_argument, NULL, 'x'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "t:r:s:c:p:i:d:vqbw",
                         long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 't':
            mboxTx = atoi(optarg);
            options |= MBOXTX;
            break;

        case 'r':
            mboxRx = atoi(optarg);
            options |= MBOXRX;
            break;

        case 's':
            g_Size = atoi(optarg);
            g_Size = (g_Size < MIN_PACKET_LEN) ? MIN_PACKET_LEN : g_Size;
            options |= PKTSIZE;
            break;

        case 'c':
            numPkts = atoi(optarg);
            options |= NUMPKTS;
            break;

        case 'p':
            preload = atoi(optarg);
            options |= PRELOAD;
            break;

        case 'i':
            memset(ifname, '\0', IFNAMSIZ);
            strcpy((char *)ifname, optarg);
            break;

        case 'd':
            duration = atoi(optarg);
            options |= DURATION;
            break;

        case 'v':
            verifyMode = TRUE;
            printf("data-verify mode selected\n");
            break;
        case 'q':
            quiet = TRUE;
            printf("running quiet...\n");
            break;
        case 'b':
            dumpdata = TRUE;
            break;
        case 'y':
            flagDelay = TRUE;
            printf("packet loopback delay enabled...this will slow down throughput results...\n");
            break;
        case 'w':
            waitresources = TRUE;
            break;
        case 0:
            /* for options that are just simple flags */
            break;
        case 'x':
            rxperfburstcnt = atoi(optarg);
            break;
        default:
            printf("invalid option : %d \n",c);
            usage();
        }
    }

    printf("netif : %s\n",ifname);

    if (flagDelay) {
        printf("packet loopback delay enabled...this will slow down throughput results...\n");
    }

    if (dumpCreditState) {
        struct ifreq ifr;
        unsigned int command;

        bindnetif();
        memset(&ifr,0,sizeof(ifr));
        strncpy(ifr.ifr_name, (char *)ifname, sizeof(ifr.ifr_name));
        command = AR6000_XIOCTL_DUMP_HTC_CREDIT_STATE;
        ifr.ifr_data = (char *)&command;

        printf("Sending command to %s to dump credit states \n",ifname);
        if (ioctl(sockfd, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            perror("ioctl");
            exit(1);
        }

        exit(0);
    }

    do {
        
        if (!((options & (MBOXTX | MBOXRX | PKTSIZE)) == (MBOXTX | MBOXRX | PKTSIZE))) {
            usage();
            break;    
        }
        
        if (randomLength) {
            printf("randomized packet lengths have been selected, range : %d to %d bytes \n",
                MIN_PACKET_LEN, g_Size);
        }

        if (verifyMode) {
            if (verfRandomData) {
                printf("verify mode uses random data\n");
            } else {
                printf("verify mode will use known pattern data\n");
            }
        }

        bindnetif();

        ident = getpid();
        setlinebuf(stdout);

            /* indicate that this stream is now active */
        IndicateTrafficActivity(mboxTx, TRUE);

        signal(SIGINT, finish);
        
        if (flagtxperf) {
            unsigned char txperfbuffer[MAX_PACKET_LEN];
            int            size;
            
            printf("Running Tx Perf Test ... \n"); 
            
            if (!(options & DURATION)) {
                printf(" *** must set duration for tx perf test !\n");   
                break; 
            }
            
            if (randomLength) {
                printf(" *** Tx perf is using random lengths, performance measurement will not be made \n"); 
            }
                /* send small dummy ping packet to reset recv count for this test */
            fillpacket(txperfbuffer, sizeof(EPPING_HEADER),EPPING_CMD_RESET_RECV_CNT,CMD_FLAGS_NO_DROP,NULL,0);       
            sendpacket(txperfbuffer, sizeof(EPPING_HEADER));    
                /* set timer to stop and collect stats */
            signal(SIGALRM, txperffinish);
            alarm(duration);
            
            while (!txperfdone) {
                 size = GetLength();
                    /* send packets down as quickly as possible. flag that these packets should
                     * not be echoed.
                     * 
                     * When the duration expires the finisher function will grab the statistics */ 
                fillpacket(txperfbuffer, size, EPPING_CMD_NO_ECHO, 0, NULL, 0);       
                sendpacket(txperfbuffer, size);
                numTxPerfTransmitted++;   
            }
                        
            break;    
        }
        
        if (flagrxperf) {
            unsigned char rxperfbuffer[MAX_PACKET_LEN];
            EPPING_CONT_RX_PARAMS  rxParams;
            
            printf ("Running RX perf test, rx burst count: %d \n",rxperfburstcnt);
            
            if (!(options & DURATION)) {
                printf(" *** must set duration for rx perf test !\n");   
                break; 
            }
            
            if (g_Size > MAX_PACKET_LEN) {
                printf(" *** Packet size (%d) is too large for RX test !\n",g_Size);  
                break;    
            }
            memset(&rxParams, 0, sizeof(rxParams));
            rxParams.BurstCnt = (A_UINT16)rxperfburstcnt;
            rxParams.PacketLength = (A_UINT16)g_Size;
            
            if (verifyMode) {
                rxParams.Flags |= EPPING_CONT_RX_DATA_CRC;
                if (verfRandomData) {
                    rxParams.Flags |= EPPING_CONT_RX_RANDOM_DATA;    
                }
                printf(" *** verify mode requested, may slow down throughput results \n");   
            }             
            
            if (randomLength) {
                rxParams.Flags |= EPPING_CONT_RX_RANDOM_LEN;                
                printf(" *** Rx Perf mode using random lengths, throughput calculation will not be made. \n");     
            }
                
                /* send initial command packet to configure the test */
            fillpacket(rxperfbuffer, 
                       sizeof(EPPING_HEADER), 
                       EPPING_CMD_CONT_RX_START,
                       CMD_FLAGS_NO_DROP,
                       (A_UINT8*)&rxParams,
                       sizeof(rxParams));  
                            
            sendpacket(rxperfbuffer, sizeof(EPPING_HEADER));    
                /* set timer to stop and collect stats */
            signal(SIGALRM, rxperffinish);
            alarm(duration);
            
            while (!rxperfdone) {
                memset(rxperfbuffer, 0, sizeof(EPPING_HEADER));
                if ((length = recvfrom(sockfd, rxperfbuffer, MAX_PACKET_LEN, 0, NULL, NULL)) < 0) {
                    NotifyError("recvfrom");
                    exit(1);
                }
                rxperf_prpack(rxperfbuffer, length);
            }
            
            break;    
        }
        
        /* normal mbox ping handling... */
        
        if (preload)
        {
            signal(SIGALRM, finish);
            for (i=0; i<preload; i++) pinger();
            alarm(duration);
        }
        else
        {
            signal(SIGALRM, catcher);
            catcher(0);
        }

        for (;;) {
            memset(rxpacket, '\0', sizeof(EPPING_HEADER));
            if ((length = recvfrom(sockfd, rxpacket, MAX_PACKET_LEN, 0, NULL, NULL)) < 0) {
                NotifyError("recvfrom");
                exit(1);
            }
            pr_pack(rxpacket, length);
            if (preload) {
                pinger();
            }
            else {
                if (numPkts && numReceived >= numPkts) {
                    finish(0);
                }
            }
        }
    
    } while (FALSE);
    
    exit(0);
}

typedef struct _TRAFFIC_IOCTL_DATA {
    A_UINT32                       Command;
    struct ar6000_traffic_activity_change Data;
} TRAFFIC_IOCTL_DATA;

void IndicateTrafficActivity(int StreamID, A_BOOL Active)
{
    struct             ifreq ifr;
    TRAFFIC_IOCTL_DATA ioctlData;

    if (StreamID >= HCI_TRANSPORT_STREAM_NUM) {
        return;    
    }
    
    memset(&ifr,0,sizeof(ifr));
    memset(&ioctlData,0,sizeof(ioctlData));

    strncpy(ifr.ifr_name, (char *)ifname, sizeof(ifr.ifr_name));
    ioctlData.Command = AR6000_XIOCTL_TRAFFIC_ACTIVITY_CHANGE;
    ioctlData.Data.StreamID = StreamID;
    ioctlData.Data.Active = Active ? 1 : 0;

    ifr.ifr_data = (char *)&ioctlData;

    //printf("Sending command to %s, stream %d is now %s \n",
    //        ifname, StreamID, Active ? "Active" : "Inactive");

    if (ioctl(sockfd, AR6000_IOCTL_EXTENDED, &ifr) < 0)
    {
        perror("ioctl - activity");
        exit(1);
    }
}


void bindnetif(void)
{
    struct ifreq ifr;

    if ((sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
        NotifyError("socket");
        exit(1);
    }

    memset(&ifr, '\0', sizeof(struct ifreq));
    strncpy(ifr.ifr_name, (char *)ifname, sizeof(ifr.ifr_name));
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        NotifyError("SIOCGIFINDEX");
        exit(1);
    }

    memset(&my_addr, '\0', sizeof(struct sockaddr_ll));
    my_addr.sll_family = AF_PACKET;
    my_addr.sll_protocol = htons(ETH_P_ALL);
    my_addr.sll_ifindex = ifr.ifr_ifindex;
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll)) < 0) {
        NotifyError("bind");
        exit(1);
    }
}

void
catcher(int signo)
{
    int waittime;

    pinger();
    if (numPkts == 0 || numTransmitted < numPkts) {
        alarm(1);
    }
    else {
        if (numReceived) {
            waittime = 2 * utmax / 1000;
            waittime = (waittime > 0) ? waittime : 1;
        }
        else {
            waittime = MAXWAIT;
        }
        signal(SIGALRM, finish);
        alarm(waittime);
    }
}



static unsigned int crc16table[256] =
{
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

unsigned short CalcCRC16(unsigned char *pBuffer, int length)
{
    unsigned int ii;
    unsigned int index;
    unsigned short crc16 = 0x0;

    for(ii = 0; ii < length; ii++) {
        index = ((crc16 >> 8) ^ pBuffer[ii]) & 0xff;
        crc16 = ((crc16 << 8) ^ crc16table[index]) & 0xffff;
    }
    return crc16;
}

/* simple dump buffer code */
void DumpBuffer(unsigned char *pBuffer, int Length, char *description)
{
    char  line[49];
    char  address[5];
    char  ascii[17];
    char  temp[5];
    int   i;
    unsigned char num;
    int offset = 0;

    printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
    printf("Description:%s  Length :%d \n",description, Length);
    printf("Offset                   Data                               ASCII        \n");
    printf("--------------------------------------------------------------------------\n");

    while (Length) {
        line[0] = (char)0;
        ascii[0] = (char)0;
        address[0] = (char)0;
        sprintf(address,"%4.4X",offset);
        for (i = 0; i < 16; i++) {
            if (Length != 0) {
                num = *pBuffer;
                sprintf(temp,"%2.2X ",num);
                strcat(line,temp);
                if ((num >= 0x20) && (num <= 0x7E)) {
                    sprintf(temp,"%c",*pBuffer);
                } else {
                    sprintf(temp,"%c",0x2e);
                }
                strcat(ascii,temp);
                pBuffer++;
                Length--;
            } else {
                    /* pad partial line with spaces */
                strcat(line,"   ");
                strcat(ascii," ");
            }
        }
        printf("%s    %s   %s\n", address, line, ascii);
        offset += 16;
    }
    printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

}

#define DATA_OFFSET             (sizeof(EPPING_HEADER))

void fillpacket(unsigned char *txpacket, int size, A_UINT16 Cmd, A_UINT16 Flags, A_UINT8 *cmdParams, int cmdParamLength)
{
    struct timeval tv;
    int remainingLength;
    int dataBytes;
    unsigned short CRC16;
    unsigned char *pDataBuffer;
    EPPING_HEADER  *pktHeader;


    /* Fill in the required book keeping informnation */

    memset(txpacket, 0, sizeof(EPPING_HEADER));
    pktHeader = (EPPING_HEADER *)txpacket;
    memset(pktHeader->_rsvd, EPPING_RSVD_FILL, sizeof(pktHeader->_rsvd));
    pktHeader->StreamEcho_h = mboxRx;     /* Receive mailbox number */
    pktHeader->StreamEchoSent_t = 0xDE;   /* fill in some values other than zero */
    pktHeader->StreamRecv_t = 0xAD;
    
    SET_EPPING_PACKET_MAGIC(pktHeader);
    
    pktHeader->Cmd_h = Cmd;
    pktHeader->CmdFlags_h = Flags;
    
    if (flagDelay) {
            /* tell the target to delay this packet */
        pktHeader->CmdFlags_h |= CMD_FLAGS_DELAY_ECHO;
    }
    
    if (cmdParams != NULL) {
        if (cmdParamLength > sizeof(pktHeader->CmdBuffer_h)) {
            printf(" invalid length for ping cmd buffer!!! %d max = %d \n",cmdParamLength, sizeof(pktHeader->CmdBuffer_h));
            exit(1);    
        }
        memcpy(pktHeader->CmdBuffer_h, cmdParams, cmdParamLength);  
    }
    
    memcpy(&pktHeader->HostContext_h, &ident, sizeof(A_UINT32)); /* Process ID */
    
    if (!flagtxperf) {
        timerclear(&tv);
        if (gettimeofday(&tv, NULL) < 0) {
            NotifyError("gettimeofday");
            exit(1);
        }
        memcpy(&pktHeader->TimeStamp[0], &tv, sizeof(struct timeval)); /* Insert the timestamp */
    }

    pktHeader->StreamNo_h = mboxTx; /* IP TOS Offset */
    pktHeader->SeqNo = numTransmitted;
    
    numTransmitted += 1;
    
    remainingLength = size - sizeof(EPPING_HEADER);
    dataBytes = 0;        
    pDataBuffer = &txpacket[sizeof(EPPING_HEADER)];

#if 0    
        /* NOTE:when using loopback testing in the HCI bridge with full framer support, we can only send
            ACL packets   */
            
        /* the EPPING header has some space for us to stick in an HCI packet header */
    if (size <= 248) {
        BT_HCI_EVENT_HEADER *pEvt = (BT_HCI_EVENT_HEADER *)&pktHeader->_HCIRsvd[0];
            /* we fixup the HCI header as an Event header so when it is echo'd back it is
             * the expected packet type */
        pEvt->EventCode = 0xDE;
        pEvt->ParamLength = (A_UINT8)size - sizeof(BT_HCI_EVENT_HEADER);
            /* if this is echo'd back to us, we want it recv'd on this channel */
        pktHeader->_HCIRsvd[HCI_RSVD_EXPECTED_PKT_TYPE_RECV_OFFSET] = HCI_UART_EVENT_PKT; 
    } else {

#endif
    
    {    
        BT_HCI_ACL_HEADER *pAcl = (BT_HCI_ACL_HEADER *)&pktHeader->_HCIRsvd[0];
        pAcl->Length = (A_UINT16)size - sizeof(BT_HCI_ACL_HEADER);
        pAcl->Flags_ConnHandle = 0x0FFF;
            /* if this packet is echo'd back to us, we want it recv'd on this channel */
        pktHeader->_HCIRsvd[HCI_RSVD_EXPECTED_PKT_TYPE_RECV_OFFSET] = HCI_UART_ACL_PKT;
    }
                
    if (verifyMode & (remainingLength > 0)) {
        unsigned char* pBuffer;
        
            /* save start of buffer */
        pBuffer = pDataBuffer;        
            /* seed the random number generator */
        srand((tv.tv_usec + numTransmitted));
            /* fill the remainder with data */
        while (remainingLength > 0) {
                /* stick in some random data */
            if (verfRandomData) {
                *pDataBuffer = (unsigned char)rand() + dataBytes;
            } else {
                *pDataBuffer = dataBytes;
            }
            remainingLength--;
            pDataBuffer++;
            dataBytes++;
        }

        CRC16 = CalcCRC16(pBuffer,dataBytes);
        pktHeader->DataCRC = CRC16;
        pktHeader->CmdFlags_h |= CMD_FLAGS_DATA_CRC;
    } else {
        /* Fill the rest of the txpacket */
        while (remainingLength > 0) {
            *pDataBuffer = dataBytes;
            pDataBuffer++;
            dataBytes++;
            remainingLength--;
        }
    } 
    
    pktHeader->DataLength = dataBytes;
        
    CRC16 = CalcCRC16(&txpacket[EPPING_HDR_CRC_OFFSET], EPPING_HDR_BYTES_CRC);
    
        /* save hdr CRC */
    pktHeader->HeaderCRC = CRC16; 
}



void sendpacket(unsigned char *txpacket, int size)
{
    struct timeval socktimeout;
    int ret;
    fd_set  writeset;
    
    FD_ZERO(&writeset);
    FD_SET(sockfd,&writeset);
    memset(&socktimeout,0,sizeof(socktimeout));
    socktimeout.tv_sec = 0;
    socktimeout.tv_usec = 100000;  /* 100 MS */

    while (1) {

            /* wait for socket to be writeable */
        if ((ret = select(sockfd + 1, NULL, &writeset, NULL, &socktimeout)) < 0) {
            NotifyError("select");
            exit(1);
        }

        if (!FD_ISSET(sockfd,&writeset)) {
            if (randomLength || flagDelay || waitresources || flagtxperf) {
                /* if the randomizer or packet delay setting is enabled, it is possible to
                 * exhaust socket resources, we can try to be more persistent here */
                continue;
            } else {
                printf("socket write could not get buffer space \n");
                NotifyError("select");
                exit(1);
            }
        }

        break;
    }

    /* Send the packet */
    if ((ret = sendto(sockfd, txpacket, size, 0, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll))) < 0) {
        NotifyError("sendto");
        exit(1);
    }

    if (ret < size) {
        printf("stream ping: wrote %d out of %d chars\n", ret, size);
        fflush(stdout);
    }
}


void
pinger(void)
{
    unsigned char txpacket[MAX_PACKET_LEN];
    int size;
    
    
    size = GetLength();
         
    fillpacket(txpacket, size, EPPING_CMD_ECHO_PACKET,0,NULL,0);
  
    if (dumpdata) {
        DumpBuffer(txpacket,size,"TX Data Dump");
    }

    sendpacket(txpacket, size);
}

void
finish(int signo)
{
    putchar('\n');
    fflush(stdout);
    printf("\n---- Stream PING Statistics ----\n");
    printf("Stream test complete. Send stream: %d loopback stream: %d \n",mboxTx, mboxRx);
    printf("Packets transmitted: %d\n", numTransmitted);
    printf("Packets received: %d\n", numReceived);
    if (verifyMode) {
        printf("Packet CRC errors: %d\n", crcErrors);
    }

    if (numTransmitted) {
       if (numReceived > numTransmitted) {
           printf("-- somebody's printing up packets!");
       }
       else {
           printf("%.2f%% Packet loss",
                  (float) ((((float)numTransmitted-(float)numReceived)*100) / (float)numTransmitted));
       }
    }
    printf("\n");
    if (preload)
    {
        if (numReceived) {
            if (randomLength) {
                printf("Throughput = %.2f pkts/sec \n", (float)numReceived/duration);
            } else {
                printf("Throughput = %.2f pkts/sec, %.2f Mbps\n", (float)numReceived/duration,
                    (2.0*(float)numReceived/(float)duration)*(float)g_Size*8.0/1000000.0);
            }
        }
    }
    else
    {
        if (numReceived) {
            printf("User round-trip (us)  min/avg/max = %d/%d/%d\n",
                   utmin, utsum/numReceived, utmax);
#if 0
            printf("Kernel round-trip (us)  min/avg/max = %d/%d/%d\n",
                   ktmin, ktsum/numReceived, ktmax);
#endif
        }
    }
    fflush(stdout);

        /* we are done, mark the traffic on this stream as inactive */
    IndicateTrafficActivity(mboxTx, FALSE);

    exit(0);
}

void txperffinish(int signo)
{
    unsigned char cmdbuffer[MAX_PACKET_LEN];
    unsigned char recvbuffer[MAX_PACKET_LEN];
    unsigned int  numTxPerfReceived = 0;
    EPPING_HEADER *pktHeader;
    int     length;
    
    txperfdone = TRUE;
         
        /* send a small dummy ping packet grab the recv count */
    fillpacket(cmdbuffer, sizeof(EPPING_HEADER), EPPING_CMD_CAPTURE_RECV_CNT, CMD_FLAGS_NO_DROP, NULL, 0);       
    sendpacket(cmdbuffer, sizeof(EPPING_HEADER));    
       
    printf("(stream:%d) waiting for recv count response from target \n",mboxTx);
    
    for (;;) {
        memset(recvbuffer, '\0', MAX_PACKET_LEN);
        if ((length = recvfrom(sockfd, recvbuffer, MAX_PACKET_LEN, 0, NULL, NULL)) < 0) {
            NotifyError("recvfrom");
            exit(1);
        }
        
        if (length < sizeof(EPPING_HEADER)) {
            printf ("!!! got : %d\n", length);
            continue;    
        }
        
        pktHeader = (EPPING_HEADER *)recvbuffer;
       
        if (IS_EPPING_PACKET(pktHeader)) {
            if ((pktHeader->HostContext_h == ident) && (pktHeader->Cmd_h == EPPING_CMD_CAPTURE_RECV_CNT)) {
                memcpy(&numTxPerfReceived, pktHeader->CmdBuffer_t, sizeof(numTxPerfReceived));
                break;    
            }
        }
    }
          
    putchar('\n');
    fflush(stdout);
    printf("\n---- Tx PERF Statistics ----\n");
    printf("Packets transmitted: %d , received by target : %d  (loss : %.2f %%) stream : %d \n", 
        numTxPerfTransmitted, numTxPerfReceived, 
        (float) ((((float)numTxPerfTransmitted-(float)numTxPerfReceived)*100) / (float)numTxPerfTransmitted), 
        mboxTx);
        
      
    printf("\n");

    if (numTxPerfReceived && !randomLength) {
        printf("Throughput = %.2f pkts/sec, bytes per pkt: %d,  %.2f Mbps\n", 
                (float)numTxPerfReceived/duration,
                g_Size,
                ((float)numTxPerfReceived/(float)duration)*(float)g_Size*8.0/1000000.0);            
    }
  
    
    fflush(stdout);

        /* we are done, mark the traffic on this stream as inactive */
    IndicateTrafficActivity(mboxTx, FALSE);

    exit(0);
}

void rxperffinish(int signo)
{
    unsigned char cmdbuffer[MAX_PACKET_LEN];
    unsigned char recvbuffer[MAX_PACKET_LEN];
    int           length;
    EPPING_HEADER *pktHeader;
    
    rxperfdone = TRUE;
    
        /* stop the continous RX */
    fillpacket(cmdbuffer, sizeof(EPPING_HEADER), EPPING_CMD_CONT_RX_STOP, CMD_FLAGS_NO_DROP, NULL, 0);       
    sendpacket(cmdbuffer, sizeof(EPPING_HEADER));    

    printf("(stream:%d) waiting for continous RX STOP response from target \n",mboxRx);
    
    for (;;) {
        memset(recvbuffer, '\0', MAX_PACKET_LEN);
        if ((length = recvfrom(sockfd, recvbuffer, MAX_PACKET_LEN, 0, NULL, NULL)) < 0) {
            NotifyError("recvfrom");
            exit(1);
        }
        
        if (length < sizeof(EPPING_HEADER)) {
            printf ("!!! got : %d\n", length);
            continue;    
        }
        
        pktHeader = (EPPING_HEADER *)recvbuffer;
       
        if (IS_EPPING_PACKET(pktHeader)) {
                /* look for command echo */
            if ((pktHeader->HostContext_h == ident) && (pktHeader->Cmd_h == EPPING_CMD_CONT_RX_STOP)) {
                break;    
            }
        }
    }
    
    fflush(stdout);
    printf("\n---- Rx PERF Statistics ----\n");
    printf("Packets received from target : %d  stream : %d \n", numReceived,mboxRx);
    if (verifyMode) {
        printf("Packet CRC errors: %d\n", crcErrors);
    }
    printf("\n");

    if (numReceived && !randomLength) {
        printf("Throughput = %.2f pkts/sec, bytes per pkt: %d,   %.2f Mbps\n", 
                (float)numReceived/duration,
                g_Size,
                ((float)numReceived/(float)duration)*(float)g_Size*8.0/1000000.0);            
    }

    fflush(stdout);
    exit(0);
}

void
tvsub(register struct timeval *out, register struct timeval *in)
{
    if((out->tv_usec -= in->tv_usec) < 0) {
        out->tv_sec--;
        out->tv_usec += 1000000;
    }
    out->tv_sec -= in->tv_sec;
}

A_BOOL verifyheader(A_UINT8 *buffer, int length)
{
    A_BOOL good = FALSE;
    EPPING_HEADER *pktHeader;
    A_UINT16      verfCRC16, CRC16;
    
    pktHeader = (EPPING_HEADER *)buffer;
        
    do {
        
        if (length < sizeof(EPPING_HEADER)) {
            break; 
        }
    
        if (!IS_EPPING_PACKET(pktHeader)) {
                /* not a EPPING packet */
            break;
        }
        
                    /* CRC check only the fixed portion of the header, the target alters some of the fields */
        CRC16 = CalcCRC16(&buffer[EPPING_HDR_CRC_OFFSET],EPPING_HDR_BYTES_CRC);
            /* save hdr CRC */
        verfCRC16  = pktHeader->HeaderCRC;
        if (CRC16 != verfCRC16) {
            printf(" ping header corrupted! CRC is: 0x%X, should be 0x%X \n",
                CRC16, verfCRC16);
            DumpBuffer(buffer,length,"Bad header");
            crcErrors++;
            break;
        }
    
        if (pktHeader->DataLength > (length - sizeof(EPPING_HEADER))) {
            printf(" DataLength reports %d bytes but packet has only %d remaining\n",
            pktHeader->DataLength, length - sizeof(EPPING_HEADER));
            DumpBuffer(buffer,length,"Bad data length");
            exit(1);
        }
        
    
        good = TRUE;
        
    } while (FALSE);
    
    return good;
}

A_BOOL verifypacket(A_UINT8 *buffer, int length)
{
    unsigned char *pBuffer = &buffer[DATA_OFFSET];
    unsigned short crc16, verifyCrc16;
    int crcLength;
    EPPING_HEADER *pktHeader;
    
    pktHeader = (EPPING_HEADER *)buffer;
    
        /* get data CRC */
    crc16 = pktHeader->DataCRC;

        /* compute CRC on the data that follows the header */
    crcLength = pktHeader->DataLength;
    if (crcLength > 0) {
        verifyCrc16 = CalcCRC16(pBuffer,crcLength);
        if (crc16 != verifyCrc16) {
            int i;
            printf("** buffer CRC error (got:0x%4.4X expecting:0x%4.4X)\n",verifyCrc16, crc16);
            crcErrors++;
            if (!verfRandomData) {
                /* if the data is a known pattern, walk the pattern and find mismatches */
                for (i = DATA_OFFSET; i < (length - 2); i++) {
                    if (buffer[i] != (unsigned char)i) {
                        printf("offset:0x%4.4X got:0x%2.2X, expecting: 0x%2.2X \n",
                            i, buffer[i], (unsigned char)i);
                    }
                }
            }
            DumpBuffer(buffer,length,"buffer CRC error");
        } else {
            return TRUE;    
        }

    } else {
        printf("*** not enough bytes to calculate CRC!! \n");
    } 
    
    return FALSE;   
}


void
pr_pack(unsigned char *buffer, int length)
{
    struct timeval tv, tp;
    int triptime;
    pid_t pid;
    int seq;
    int txh, rxh, txt, rxt;
    EPPING_HEADER *pktHeader;

    if (!verifyheader(buffer, length)) {
        return;    
    }
    
    gettimeofday(&tv, NULL);
    pktHeader = (EPPING_HEADER *)buffer;      
    txh = pktHeader->StreamNo_h;   
    rxh = pktHeader->StreamEcho_h; 
    txt = pktHeader->StreamEchoSent_t; 
    rxt = pktHeader->StreamRecv_t;    
    pid = pktHeader->HostContext_h;

//    if ((pid != ident) || (txh != rxt) || (rxh != txt)) {
    if (pid != ident) {
            /* not our packet, could be another instance running */
        return;
    }
    
    /* the target is suppose to echo these back */
    if (txt != mboxTx) {
        printf(" Target did not echo send stream correctly, was:%d  should be %d\n",
            txt,mboxTx);
        DumpBuffer(buffer,length,"Bad header");
        exit(1);
    }

    if (rxt != mboxRx) {
        printf(" Target did not echo recv stream correctly, was:%d  should be %d\n",
            rxt,mboxRx);
        DumpBuffer(buffer,length,"Bad header");
        exit(1);

    }

    memcpy(&tp, &pktHeader->TimeStamp[0], sizeof(struct timeval));
    tvsub(&tv, &tp);
    triptime = tv.tv_sec*1000000 + tv.tv_usec;
    utsum += triptime;
    utmin = (triptime < utmin) ? triptime : utmin;
    utmax = (triptime > utmax) ? triptime : utmax;

    seq = pktHeader->SeqNo;
    
    if (preload) {
        if (!quiet) {
            printf(".");
        }
    } else {
        printf("Sequence Number: %d, Triptime: %d\n", seq, triptime);
    }

    if (seq < numReceived) {
        printf("** Sequence Number: %d, should be >= %d.  Send stream: %d loopback on: %d \n",
                seq, numReceived, mboxTx, mboxRx);

    }
    
    numReceived++;

    if (dumpdata) {
        DumpBuffer(buffer,length,"RX dump");
    }

    if (verifyMode) {
        if (!verifypacket(buffer, length)) {
             printf("Sequence Number: %d, buffer CRC error (stream TX:%d: stream RX:%d)\n",
                 seq, mboxTx, mboxRx);    
        }
    }
}

void rxperf_prpack(unsigned char *buffer, int length)
{
    int            seq;
    EPPING_HEADER *pktHeader;

    if (!verifyheader(buffer, length)) {
        return;    
    }
    
    pktHeader = (EPPING_HEADER *)buffer;      
   
    if (pktHeader->StreamNo_h != mboxRx) {
            /* not ours */
        return;    
    }
    
    seq = pktHeader->SeqNo;
    
    if (seq < numReceived) {
        printf("** RxPerf, Sequence Number: %d, should be >= %d \n", seq, numReceived);
    }

    if (!randomLength) {
        if (length != g_Size) {
             printf("**Ping packet is not expected size (%d) should be %d bytes \n", length, g_Size);   
             DumpBuffer(buffer,length,"Bad Packet"); 
        }
    }
    
    numReceived++;

    if (dumpdata) {
        DumpBuffer(buffer,length,"RX dump");
    }

    if (verifyMode) {
        if (!verifypacket(buffer, length)) {
             printf("Sequence Number: %d, buffer CRC error \n",seq);
        }
    }
}

