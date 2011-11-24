/*
 * Copyright (c) 2004-2010 Atheros Communications Inc.
 * All rights reserved.
 *
 *
 * 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	Wifi driver for AR6002
// </summary>
//
 * 
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <netinet/in.h> 
#include <netinet/tcp.h> 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <a_config.h>
#include <a_osapi.h>
#include <athdefs.h>
#include <a_types.h>
#include <wmi.h>
#include <assert.h>
#include <time.h>
#define HTC_RAW_INTERFACE 1

#ifdef DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...) do { } while (0)
#endif 

#include "athdrv_linux.h"
#include "artagent.h"

typedef PREPACK struct htc_raw_param {
    int op;
    int ep;
    int len;
    char buf[LINE_ARRAY_SIZE];
} POSTPACK HTC_RAW_PARAM;

static int      sid, cid, aid;
static char     ar6kifname[32];

int art_htc_raw_ioctl(int sockid, HTC_RAW_PARAM *param)
{
    struct ifreq  ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, ar6kifname, sizeof(ifr.ifr_name));
    ifr.ifr_data = (char*)param;
    if (ioctl(sockid, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        printf("[%s] Open ioctl for RAW HTC failed\n", __FUNCTION__);
        return -1;
    }
    return 0;
}

int art_htc_raw_open(int sockid)
{
    struct ifreq    ifr;

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, ar6kifname, sizeof(ifr.ifr_name));
    ifr.ifr_data = (char *)malloc(12);;

    ((int *)ifr.ifr_data)[0] = AR6000_XIOCTL_HTC_RAW_OPEN;
    if (ioctl(sockid, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        printf("[%s] ioctl for HTC raw write failed\n", __FUNCTION__);
        free(ifr.ifr_data);
        return -1;
    }
    free(ifr.ifr_data);
    return 0;
}

int art_htc_raw_close(int sockid)
{
    struct ifreq    ifr;

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, ar6kifname, sizeof(ifr.ifr_name));
    ifr.ifr_data = (char *)malloc(12);;

    ((int *)ifr.ifr_data)[0] = AR6000_XIOCTL_HTC_RAW_CLOSE;
    if (ioctl(sockid, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        printf("[%s] ioctl for HTC raw write failed\n", __FUNCTION__);
        free(ifr.ifr_data);
        return -1;
    }
    free(ifr.ifr_data);
    return 0;
}

int art_htc_raw_write(int sockid, unsigned char *buf, int buflen)
{
    int             ret;
    struct ifreq    ifr;

    //DPRINTF("[%s] Enter, data length = %d\n", __FUNCTION__, buflen);
   
    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, ar6kifname, sizeof(ifr.ifr_name));
    ifr.ifr_data = (char *)malloc(12 + buflen);;

    ((int *)ifr.ifr_data)[0] = AR6000_XIOCTL_HTC_RAW_WRITE;
    ((int *)ifr.ifr_data)[1] = SEND_ENDPOINT;
    ((int *)ifr.ifr_data)[2] = buflen;
    memcpy(&(((char *)(ifr.ifr_data))[12]), (char *)buf, buflen);

    if (ioctl(sockid, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        printf("[%s] ioctl for HTC raw write failed\n", __FUNCTION__);
        free(ifr.ifr_data);
        return 0;
    }

    ret = ((int *)ifr.ifr_data)[0];
    free(ifr.ifr_data);

   // DPRINTF("[%s] Exit, return value %d\n", __FUNCTION__, ret);

    return ret;
}

int art_htc_raw_read(int sockid, unsigned char *buf, int buflen)
{
    int             ret;
    struct ifreq    ifr;

   // DPRINTF("[%s] Enter, length = %d\n", __FUNCTION__, buflen);

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, ar6kifname, sizeof(ifr.ifr_name));

    ifr.ifr_data = (char *)malloc(12 + buflen);
    ((int *)ifr.ifr_data)[0] = AR6000_XIOCTL_HTC_RAW_READ;
    ((int *)ifr.ifr_data)[1] = SEND_ENDPOINT;
    ((int *)ifr.ifr_data)[2] = buflen;

    if (ioctl(sockid, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
        printf("[%s] ioctl for HTC raw read failed\n", __FUNCTION__);
        free(ifr.ifr_data);
        return 0;
    }
    ret = ((A_UINT32 *)ifr.ifr_data)[0];
    memcpy(buf, &(((char *)(ifr.ifr_data))[4]), ret);
    free(ifr.ifr_data);

    //DPRINTF("[%s] Exit, length = %d\n", __FUNCTION__, ret);

    return ret;
}

void art_delay(int usec)
{
    DPRINTF("Wait for %d usec\n", usec);
    usleep(usec);
}

static void
cleanup(int sig)
{
    if (aid>=0) {
        art_htc_raw_close(aid);
        close(aid);
    }
    if (cid>=0) {
        close(cid);
    }
    if (sid>=0) {
        close(sid);
    }
}


int sock_init(int port)
{
    int                sockid;
    struct sockaddr_in myaddr;
    socklen_t          sinsize;
    int                i, res;

    /* Create socket */
    sockid = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockid == -1) { 
        perror(__FUNCTION__);
        printf("Create socket to PC failed\n");
        return -1;
    } 

    i = 1;
    res = setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, (char *)&i, sizeof(i));
    if (res == -1) {
        close(sockid);
        return -1;
    }

    i = 1;
    res = setsockopt(sockid, IPPROTO_TCP, TCP_NODELAY, (char *)&i, sizeof(i));
    if (res == -1) {
        close(sockid);
        return -1;
    }


    myaddr.sin_family      = AF_INET; 
    myaddr.sin_port        = htons(port); 
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    memset(&(myaddr.sin_zero), 0, 8);
    res = bind(sockid, (struct sockaddr *)&myaddr, sizeof(struct sockaddr));
    if (res != 0) { 
        perror(__FUNCTION__);
        printf("Bind failed\n");
		close(sockid);
        return -1;
    } 
    if (listen(sockid, 4) == -1) { 
        perror(__FUNCTION__);
        printf("Listen failed\n");
		close(sockid);
        return -1;
    } 

    printf("Waiting for client to connect...\n");
    sinsize = sizeof(struct sockaddr_in); 
    if ((cid = accept(sockid, (struct sockaddr *)&myaddr, &sinsize)) == -1) { 
        printf("Accept failed\n");
		close(sockid);
        return -1;
    } 
    i = 1;
    res = setsockopt(cid, IPPROTO_TCP, TCP_NODELAY, (char *)&i, sizeof(i));
    if (res == -1) {
        printf("cannot set NOdelay for cid\n");
        close(sockid);
        return -1;
    }
    printf("Client connected!\n");

    return sockid;
}

int sock_recv(int sockid, unsigned char *buf, int buflen)
{
    int recvbytes;
    recvbytes = recv(sockid, buf, buflen, 0);
    if (recvbytes == 0) {
        DPRINTF("Connection close!? zero bytes received\n");
        return -1;
    } else if (recvbytes > 0) {
        return recvbytes;
    } 
    return -1;
}

int sock_send(int sockid, unsigned char *buf, int bytes)
{
	int cnt;
    unsigned char* bufpos = buf;
    while (bytes) {
        cnt = write(sockid, bufpos, bytes);

        if (!cnt) {
            break;
        }
        if (cnt == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }

        bytes -= cnt;
        bufpos += cnt;
    }
    return (bufpos - buf);    
}

static void print_help(char *pname)
{
    printf("An agent program to connect ART host and AR6K device, must be\n");
    printf("started after AR6K device driver is installed.\n\n");
    printf("Usage: %s ifname fragsize\n\n", pname);
    printf("  ifname      AR6K interface name\n");
    printf("  fragsize    Fragment size, must be multiple of 4\n\n");
    printf("Example:\n");
    printf("%s eth1 80\n\n", pname);
}

int main (int argc, char **argv)
{
	int recvbytes=0;
	int chunkLen = 0;
	unsigned int readLength = 0;
	unsigned char	*bufpos;  
    int reducedARTPacket = 1;
    A_UINT8  line[LINE_ARRAY_SIZE];
    int              frag_size = 768;
	int              port = ART_PORT;
    struct sigaction sa;

    DPRINTF("setup signal\n");
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = SA_NOCLDSTOP;
    sa.sa_handler = cleanup;
    DPRINTF("before call sigaction\n");
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);    
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);

    cid = sid = aid = -1;
    DPRINTF("setup ifname\n");
    memset(ar6kifname, '\0', sizeof(ar6kifname));

    if (argc == 1 ) {
        print_help(argv[0]);
        return -1;
    }
    if (argc > 1 ) {
        strcpy(ar6kifname, argv[1]);
    } else {
        strcpy(ar6kifname, "wlan0");
    }
    if (argc > 2) {
        frag_size = atoi(argv[2]);
    }
    if (argc > 3) {
        port = atoi(argv[3]);
    }
	if (port == 0) 
		port = ART_PORT;
	else if (port < 0 || port >65534) {
		printf("Invalid port number\n");
		goto main_exit;
	}

    if ((frag_size == 0) || ((frag_size % 4) != 0))
    {
        printf("Invalid fragsize, should be multiple of 4\n");
        goto main_exit;
    }

    aid = socket(AF_INET, SOCK_DGRAM, 0);
    if (aid < 0) 
    {
        printf("Create socket to AR6002 failed\n");
        goto main_exit;
    }

    DPRINTF("try to open htc\n");
    if (art_htc_raw_open(aid) < 0)
    {
        printf("HTC RAW open on %s interface failed\n", argv[1]);
        goto main_exit;
    }
    DPRINTF("open sock\n");
    sid = sock_init(port);
    if (sid < 0) {
        printf("Create socket to ART failed\n");
        cleanup(0);
        return -1;
    }

	if ((recvbytes=sock_recv(cid, line, LINE_ARRAY_SIZE)) < 0) {
        printf("Cannot nego packet size\n");
                cleanup(0);
		return -1;
	}
    DPRINTF("Get nego bytes %d\n", recvbytes);
	if (1 == (*(unsigned int *)line)) {
		reducedARTPacket = 1;
	}
	else {
		reducedARTPacket = 0;
	}
	sock_send(cid, &(line[0]), 1);

    DPRINTF("Ready to loop for art packet reduce %d\n", reducedARTPacket);
    while (1) {
        //DPRINTF("wait for tcp socket\n");
        if ((recvbytes = sock_recv(cid, line, LINE_ARRAY_SIZE)) < 0) {
            printf("Cannot recv packet size %d\n", recvbytes);
            cleanup(0);
            return -1;
        }
        if (!reducedARTPacket) {
            bufpos = line;
            while (recvbytes) {
                if (recvbytes > frag_size) {
                    chunkLen = frag_size;
                } else {
                    chunkLen = recvbytes;
                }

                art_htc_raw_write(aid, bufpos, chunkLen);

                recvbytes-=chunkLen;
                bufpos+=chunkLen;
            }
        } else {
            //DPRINTF("Get %d byte from tcp and Write to htc\n", recvbytes);
            art_htc_raw_write(aid, line, recvbytes);

            while (frag_size == recvbytes) {
                sock_send(cid, &(line[0]), 1);
                if ((recvbytes = sock_recv(cid, line, LINE_ARRAY_SIZE)) < 0) {
                    printf("Cannot recv packet size %d\n", recvbytes);
                    cleanup(0);
                    return -1;
                }
                //RETAILMSG (1,(L"ART_Receive %d\n",recvbytes));
                //DPRINTF("Get %d from next tcp and Write to htc\n", recvbytes);
                if (0xbeef == *((A_UINT16 *)line)) {
                    // end marker
                    break;
                } else
                    art_htc_raw_write(aid, line, recvbytes);
            }

        }
        art_htc_raw_read(aid, (unsigned char*)&readLength, 4);
        art_htc_raw_read(aid, line, readLength);
        if ((REG_WRITE_CMD_ID != line[0]) && (MEM_WRITE_CMD_ID != line[0]) &&
            (M_PCI_WRITE_CMD_ID != line[0]) && (M_PLL_PROGRAM_CMD_ID != line[0]) &&
            (M_CREATE_DESC_CMD_ID != line[0])) {
            //DPRINTF("Send back ART packet %d\n", readLength);
            sock_send(cid, line, readLength);
        } else {
            //DPRINTF("Send back ART packet ACK Command %d\n", (int)/line[0]);
            sock_send(cid, &(line[0]), 1);
        }
    }
		

main_exit:
    printf("Normal exit\n");
    cleanup(0);
    return 0;
}

