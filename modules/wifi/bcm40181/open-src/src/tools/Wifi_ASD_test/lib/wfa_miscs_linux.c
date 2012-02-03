/*
 * Linux port of asd command line utility
 *
 * Copyright 2002, Broadcom Corporation
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Broadcom Corporation.
 *
 */
#include <arpa/inet.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "wfa_debug.h"
#include "wfa_main.h"
#include "wfa_types.h"
#include "wfa_tg.h"
#include "wfa_miscs.h"
#include "wfa_cmds.h"

#ifndef SIOCGIWNAME
#define SIOCGIWNAME 0x8B01
#endif
extern unsigned short wfa_defined_debug;
extern tgStream_t *theStreams;
extern char PingStr[];
tgStream_t *findStreamProfile(int id);
void
asd_sleep(int SleepTime)
{

	sleep(SleepTime);

}

void
uapsd_sleep(int SleepTime)
{

	usleep(SleepTime);

}

void
asd_closeSocket(int sockfd)
{
	close(sockfd);

}
void
asd_shutDown(int Sockfd)
{
	shutdown(Sockfd, SHUT_WR);

}
void
exec_process(char* command)
{
	system(command);
}

int
Start_Socket_Service()
{
	return 0;
}
int
Stop_Socket_Service()
{
	return 0;
}

void 
wfaGetSockOpt(int sockfd, int* tosval, socklen_t* size)
{
	getsockopt(sockfd, IPPROTO_IP, IP_TOS, tosval, size);
}

int
wfaSetSockOpt(int sockfd, int* tosval, int size)
{
	int sockOpt;
	sockOpt = setsockopt(sockfd, IPPROTO_IP, IP_TOS, tosval, size);
	return sockOpt;
}

int
wfa_estimate_timer_latency()
{
	struct timeval t1, t2, tp2;
	int sleep=20000; /* two miniseconds */
	int latency =0;

	gettimeofday(&t1, NULL);
	usleep(sleep);

	gettimeofday(&t2, NULL); 

	tp2.tv_usec = t1.tv_usec + 20000;
	if( tp2.tv_usec >= 1000000)
	{
		tp2.tv_sec = t1.tv_sec +1;
		tp2.tv_usec -= 1000000;
	}
	else
		tp2.tv_sec = t1.tv_sec;

	return latency = (t2.tv_sec - tp2.tv_sec) * 1000000 + (t2.tv_usec - tp2.tv_usec); 
}

/* This function returns the wireless adapter in Linux systems. 
 * This is for getting wireless adapter name programmatically */
void
GetWirelessAdapter(char *adapter_name)
{
	char	buf[1024];
	struct	ifconf ifc;
	struct	ifreq *ifr;
	int	sck;
	int	nInterfaces;
	int	i;
	struct utsname name;
	uname(&name);
	/* Checking for x86 architecture */
	if (!strcmp(name.machine, "i386") || !strcmp(name.machine, "i686") || !strcmp(name.machine, "i586")) {
			
		/* Get a socket handle. */
		sck = socket(AF_INET, SOCK_DGRAM, 0);
		if (sck < 0) {
			perror("socket");
			return ;
		}

		/* Query available interfaces. */
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = buf;
		if (ioctl(sck, SIOCGIFCONF, &ifc) < 0) {
			perror("ioctl(SIOCGIFCONF)");
			close (sck);
			return ;
		}

		/* Iterate through the list of interfaces. */
		ifr = ifc.ifc_req;
		nInterfaces = ifc.ifc_len / sizeof(struct ifreq);
		for (i = 0; i < nInterfaces; i++) {
			struct ifreq *item = &ifr[i];
			if (ioctl(sck, SIOCGIWNAME, item) == 0) {
				strncpy(adapter_name,item->ifr_name, sizeof(item->ifr_name));
				close(sck);
				return;
			}
		}
		close (sck);
	}
#ifdef TARGETENV_android
	else { /* If its Android */
		strncpy (adapter_name, "eth0", 15);
	}
#else
	else { /* If its ARM */ 
		strncpy (adapter_name,"eth1", 15);
	}
#endif
	return;

}



FILE*
asd_Config(char *strFunct, char *strstrParams)
{
	FILE* fp = NULL;
	if ((fp = popen(strFunct, "r")) == NULL) {
		printf("popen failed\n");
		return NULL;
	}
	return fp;
}
void
Cleanup_File(FILE* fp)
{
	pclose(fp);
}

int exec_process_cnclient (char *buf, char *rwl_client_path, int rwl_wifi_flag)
{
	int pid, status,timeout_count = 30;
	FILE *tmpfd;
	char gCmdStr[WFA_CMD_STR_SZ];

	if (rwl_wifi_flag) {
		pid = fork();
		if (pid == 0) {
			if ((status=execl (SH_PATH, "sh", "-c", buf, NULL)) == -1)
				exit (1);		
		}
		/*The server can be in different channel after association
		 *Hence we issue a findserver and then find out if it has
		 *been associated to return the right status
		 */	
		asd_sleep(3);
		sprintf(gCmdStr, "%s findserver", rwl_client_path);
		pid = fork();
		if (pid == 0) {
			if ((status=execl (SH_PATH, "sh", "-c", gCmdStr, NULL)) == -1)
				exit (1);		
		}
		waitpid(pid,&status,0);
	}
	else {
		pid = fork();
		if (pid == 0) {
			if ((status=execl (SH_PATH, "sh", "-c", buf, NULL)) == -1)
				exit (1);		
		}
		waitpid(pid,&status,0);
	}
	/* while cnClient associates in the child, parent looks 
	 * for association if it has happened
	 * If it has not associated within a loop of 30, it comes out
	 * as not associated
	 */ 
	while(timeout_count > 0){
		sprintf(gCmdStr, "%s assoc | wc -l", rwl_client_path);
		if((tmpfd = asd_Config(gCmdStr,TEMP_FILE_PATH)) == NULL){
			DPRINT_ERR(WFA_ERR, "\nassoc failed\n");
			return FALSE;
			}
		fgets(gCmdStr, sizeof(gCmdStr), tmpfd);
		/* Short response means not associated */
		if (atoi(gCmdStr) > 2)
			break;
		/* End: Added as per BRCM 1.3 ASD */
		Cleanup_File(tmpfd);
		asd_sleep(1);
		timeout_count--;
	}
	if(timeout_count)
		return TRUE;
	else
		return FALSE;
}

void p_error(char *errorString)
{
	perror(errorString);
}
