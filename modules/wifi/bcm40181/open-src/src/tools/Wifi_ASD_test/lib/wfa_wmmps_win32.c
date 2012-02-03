/*
 * Windows port of asd command line utility
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

#include "wfa_sock.h"
#include "wfa_types.h"
#include "wfa_tg.h"
#include "wfa_ca.h"
#include "wfa_wmmps.h"
#include "wfa_miscs.h"
#include "wfa_main.h"
#include "wfa_debug.h"

extern int psSockfd;
extern int msgsize;
extern int resetsnd;
extern int gtgWmmPS;
extern int num_stops;
extern int num_hello;
extern tgWMM_t wmm_thr[];
tgThrData_t tdata[WFA_THREADS_NUM]; 
extern HANDLE thr_flag_cond;
extern HANDLE thr_stop_cond;
extern wfaWmmPS_t wmmps_info;
extern unsigned int psTxMsg[512];

extern void wfaSetDUTPwrMgmt(int mode);
extern void mpx(char *m, void *buf_v, int len);
extern int wfaTGSetPrio(int sockfd, int tgClass);
extern StationProcStatetbl_t stationProcStatetbl[LAST_TEST+1][11];
extern int receiver(unsigned int *rmsg,int length,int tos,unsigned int type);

/* WfaRcvVOCyclic: This is meant for the L1 test case. The function
** receives the VO packets from the console */
int WfaRcvVOCyclic(unsigned int *rmsg,int length,int *state)
{
	int r;
	tgWMM_t *my_wmm = &wmm_thr[wmmps_info.ps_thread];

	if(rmsg[10] != APTS_STOP)
	{
		if ((r=receiver(rmsg,length,TOS_VO,APTS_DEFAULT))>=0);
		else
			PRINTF("\nBAD REC in VO%d\n",r);
	}
	else
	{
		while(!my_wmm->stop_flag)
		{
			while(WaitForSingleObject(thr_stop_cond, 0)!=WAIT_OBJECT_0);
		}
		ResetEvent(thr_stop_cond);
		my_wmm->stop_flag = 0;
		gtgWmmPS = 0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
		wfaSetDUTPwrMgmt(PS_OFF);
		uapsd_sleep(1);
	}
	return 0;
}


/* WfaRcvStop: This function receives the stop message from the
** console, it waits for the sending thread to have sent the stop before
** quitting*/
int WfaRcvStop(unsigned int *rmsg,int length,int *state)
{
	tgWMM_t *my_wmm = &wmm_thr[wmmps_info.ps_thread];
	my_wmm->stop_flag = 0;
	PRINTF("\r\nEnterring Wfarcvstop\n");
	if(rmsg[10] != APTS_STOP)
	{
		//PRINTF("\nBAD REC in rcvstop%d\n",r);
		//WfaStaResetAll();
	}
	else
	{
		while(!my_wmm->stop_flag)
		{
			while(WaitForSingleObject(thr_stop_cond, 0)!=WAIT_OBJECT_0);
		}
		num_stops=0;
		ResetEvent(thr_stop_cond);
		my_wmm->stop_flag = 0;
		gtgWmmPS = 0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
		wfaSetDUTPwrMgmt(PS_OFF);
		uapsd_sleep(1);
	}
	return 0;
}

/*
 * wfaStaSndHello(): This function sends a Hello packet 
 *                and sleeps for sleep_period, the idea is
 *                to keep sending hello packets till the console
 *                responds, the function keeps a check on the MAX
 *                Hellos and if number of Hellos exceed that it quits
 */
int WfaStaSndHello(char psave,int sleep_period,int *state)
{
	int r;
	tgWMM_t *my_wmm = &wmm_thr[wmmps_info.ps_thread];
	if(!(num_hello++))
		create_apts_msg(APTS_HELLO, psTxMsg,0);

	r = wfaTrafficSendTo(psSockfd, (char *)psTxMsg, msgsize, (struct sockaddr *)&wmmps_info.psToAddr);
	uapsd_sleep(sleep_period);
	if(my_wmm->thr_flag)
	{
		(*state)++;
		num_hello=0;
		my_wmm->thr_flag=0;
	}
	if(num_hello > MAXHELLO)
	{
		DPRINT_ERR(WFA_ERR, "Too many Hellos sent\n");
		gtgWmmPS = 0;
		num_hello=0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
	} 
	return 0;
}

/*
 * WfaStaWaitStop(): This function sends the stop packet on behalf of the
 *                   station, the idea is to keep sending the Stop
 *                   till a stop is recd from the console,the functions
 *                   quits after stop threshold reaches.
 */
int WfaStaWaitStop(char psave,int sleep_period,int *state)
{
	int r;
	int myid=wmmps_info.ps_thread;
	PRINTF("\n Entering Sendwait");
	uapsd_sleep(sleep_period);
	if(!num_stops)
	{
		wfaSetDUTPwrMgmt(psave);
		wfaTGSetPrio(psSockfd, TG_WMM_AC_BE);
	}

	num_stops++;
	create_apts_msg(APTS_STOP, psTxMsg,wmmps_info.my_sta_id);
	r = wfaTrafficSendTo(psSockfd, (char *)psTxMsg, msgsize, (struct sockaddr *)&wmmps_info.psToAddr);
	mpx("STA msg",psTxMsg,64);

	wmm_thr[myid].stop_flag = 1;
	SetEvent(thr_stop_cond);
	if(num_stops > MAX_STOPS)
	{
		DPRINT_ERR(WFA_ERR, "Too many stops sent\n");
		gtgWmmPS = 0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
	}
	wfaSetDUTPwrMgmt(PS_OFF);
	return 0;
}

void Uapsd_Recv_Thread(LPVOID *thr_param)
{
	int myId = ((tgThrData_t *)thr_param)->tid;
	tgWMM_t *my_wmm = &wmm_thr[myId];
	tgStream_t *myStream = NULL;
	tgThrData_t *tdata =(tgThrData_t *) thr_param;
	StationProcStatetbl_t  curr_state;
	while(!my_wmm->thr_flag)
	{
		while(WaitForSingleObject(thr_flag_cond, 0)!=WAIT_OBJECT_0);
	}
	ResetEvent(thr_flag_cond);
	my_wmm->thr_flag = 0;
	wmmps_info.sta_test = B_D;
	wmmps_info.ps_thread = myId;
	wmmps_info.rcv_state = 0;
	wmmps_info.tdata = tdata;
	wmmps_info.dscp = wfaTGSetPrio(psSockfd, TG_WMM_AC_BE);
	tdata->state_num=0;
	/*
	* default timer value
	*/
	while(gtgWmmPS>0)
	{
		if(resetsnd)
		{
			tdata->state_num = 0;
			resetsnd = 0;
		}
		tdata->state =  stationProcStatetbl[wmmps_info.sta_test];
		curr_state = tdata->state[tdata->state_num];
		curr_state.statefunc(curr_state.pw_offon,curr_state.sleep_period,&(tdata->state_num));
	}

}

void init_wmmps_thr()
{
	int cntThr = 0;
	DWORD UAPSDThread;
	/*Create the thread event for multiple streams and hold the thread handles in the array*/
	thr_flag_cond = CreateEvent(NULL, FALSE, TRUE, NULL);
	thr_stop_cond = CreateEvent(NULL, FALSE, TRUE, NULL);
	for(cntThr = 0; cntThr< 1; cntThr++)
	{
		tdata[cntThr].tid = cntThr;
		wmm_thr[cntThr].thr_id = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Uapsd_Recv_Thread, (LPVOID) &tdata[cntThr], 0, &UAPSDThread);
		if (wmm_thr[cntThr].thr_id == NULL){
			printf("CreateThread Failed:%d\n",WSAGetLastError());
		}
		sleep(1);
		wmm_thr[cntThr].thr_flag = 0;
	}
}

int WmmpsTrafficRecv(int sock, char *buf, struct sockaddr *from)
{
	int bytesRecvd;
	socklen_t  addrLen;
	struct sockaddr_in recFrom;
	memset(&recFrom, 0, sizeof(recFrom));
	addrLen = sizeof(recFrom);
	bytesRecvd = recvfrom(sock, buf, MAX_UDP_LEN, 0, (struct sockaddr *)&recFrom, &addrLen);
	return bytesRecvd;
}
