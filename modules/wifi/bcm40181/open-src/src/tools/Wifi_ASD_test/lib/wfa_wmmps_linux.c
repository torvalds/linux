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

#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "wfa_sock.h"
#include "wfa_types.h"
#include "wfa_tg.h"
#include "wfa_ca.h"
#include "wfa_wmmps.h"
#include "wfa_miscs.h"
#include "wfa_main.h"
#include "wfa_debug.h"
#include "wfa_tlv.h"

extern int psSockfd;
extern int msgsize;
extern int resetsnd;
extern int gtgWmmPS;
extern int num_stops;
extern int num_hello;
extern tgWMM_t wmm_thr[];
extern wfaWmmPS_t wmmps_info;
extern unsigned int psTxMsg[512];
tgThrData_t tdata[WFA_THREADS_NUM]; 

extern void tmout_stop_send(int num);
extern void wfaSetDUTPwrMgmt(int mode);
extern void wfaSetThreadPrio(int tid, int class);
extern void mpx(char *m, void *buf_v, int len);
extern int wfaTGSetPrio(int sockfd, int tgClass);
extern tgStream_t *findStreamProfile(int id);
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
		pthread_mutex_lock(&my_wmm->thr_stop_mutex);
		while(!my_wmm->stop_flag)
		{
			pthread_cond_wait(&my_wmm->thr_stop_cond, &my_wmm->thr_stop_mutex);
		}
		pthread_mutex_unlock(&my_wmm->thr_stop_mutex);
		my_wmm->stop_flag = 0;
		gtgWmmPS = 0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
		signal(SIGALRM, SIG_IGN);
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
		pthread_mutex_lock(&my_wmm->thr_stop_mutex);
		while(!my_wmm->stop_flag)
		{
			printf("\r\n stuck\n");
			pthread_cond_wait(&my_wmm->thr_stop_cond, &my_wmm->thr_stop_mutex);
		}
		num_stops=0;
		pthread_mutex_unlock(&my_wmm->thr_stop_mutex);
		my_wmm->stop_flag = 0;
		gtgWmmPS = 0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
		signal(SIGALRM, SIG_IGN);
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
	pthread_mutex_lock(&my_wmm->thr_flag_mutex);
	if(my_wmm->thr_flag)
	{
		(*state)++;
		num_hello=0;
		my_wmm->thr_flag=0;
	}
	pthread_mutex_unlock(&my_wmm->thr_flag_mutex);
	if(num_hello > MAXHELLO)
	{
		DPRINT_ERR(WFA_ERR, "Too many Hellos sent\n");
		gtgWmmPS = 0;
		num_hello=0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
		signal(SIGALRM, SIG_IGN);
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
	pthread_mutex_lock(&wmm_thr[myid].thr_stop_mutex);
	pthread_cond_signal(&wmm_thr[myid].thr_stop_cond);
	pthread_mutex_unlock(&wmm_thr[myid].thr_stop_mutex);
	if(num_stops > MAX_STOPS)
	{
		DPRINT_ERR(WFA_ERR, "Too many stops sent\n");
		gtgWmmPS = 0;
		asd_closeSocket(psSockfd);
		psSockfd = -1;
		signal(SIGALRM, SIG_IGN);
	}
	wfaSetDUTPwrMgmt(PS_OFF);
	return 0;
}

void * wfa_wmm_thread(void *thr_param)
{
	int myId = ((tgThrData_t *)thr_param)->tid;
	tgWMM_t *my_wmm = &wmm_thr[myId];
	tgStream_t *myStream = NULL;
	tgThrData_t *tdata =(tgThrData_t *) thr_param;
	int myStreamId;
	int mySock = -1; 
	int status, respLen;
	tgProfile_t *myProfile;
	StationProcStatetbl_t  curr_state;

	BYTE respBuf[WFA_BUFF_1K];
	pthread_attr_t tattr;

	pthread_attr_init(&tattr);
	pthread_attr_setschedpolicy(&tattr, SCHED_RR);
	while(1)
	{
		pthread_mutex_lock(&my_wmm->thr_flag_mutex);
		/* it needs to reset the thr_flag to wait again */
		while(!my_wmm->thr_flag)
		{
			/*
			* in normal situation, the signal indicates the thr_flag changed to
			* a valid number (stream id), then it will go out the loop and do
			* work.
			*/
			pthread_cond_wait(&my_wmm->thr_flag_cond, &my_wmm->thr_flag_mutex);
		}
		sleep(2);
		pthread_mutex_unlock(&my_wmm->thr_flag_mutex);

		myStreamId = my_wmm->thr_flag;
		my_wmm->thr_flag = 0;
		/* use the flag as a stream id to file the profile */
		myStream = findStreamProfile(myStreamId);
		myProfile = &myStream->profile;

		if(myProfile == NULL)
		{
			status = STATUS_INVALID;
			wfaEncodeTLV(WFA_TRAFFIC_AGENT_SEND_RESP_TLV, 4, (BYTE *)&status, respBuf);
			respLen = WFA_TLV_HDR_LEN+4;
			/*
			* send it back to control agent.
			*/
			continue;
		}

		switch(myProfile->direction)
		{
		case DIRECT_SEND:
			mySock = wfaCreateUDPSock(myProfile->sipaddr, myProfile->sport);
			mySock = wfaConnectUDPPeer(mySock, myProfile->dipaddr, myProfile->dport);
			/*
			* Set packet/socket priority TOS field
			*/
			wfaTGSetPrio(mySock, myProfile->trafficClass);

			/*
			* set a proper priority
			*/
			wfaSetThreadPrio(myId, myProfile->trafficClass);
#ifdef DEBUG
			printf("wfa_wmm_thread: myProfile->startdelay %i\n", myProfile->startdelay);
#endif /* DEBUG */
			/* if delay is too long, it must be something wrong */
			if(myProfile->startdelay > 0 && myProfile->startdelay < 50)
			{
				asd_sleep(myProfile->startdelay);
			}

			/*
			* set timer fire up
			*/
			signal(SIGALRM, tmout_stop_send);
			alarm(myProfile->duration);

			/* Whenever frameRate is 0,sending data at the maximum possible rate */	
			if (myProfile->rate != 0) 
				wfaSendLongFile(mySock, myStreamId, respBuf, &respLen );
			else
				wfaImprovePerfSendLongFile(mySock, myStreamId, respBuf, &respLen );

			memset(respBuf, 0, WFA_BUFF_1K);
			if (mySock != -1) {
				asd_closeSocket(mySock);
				mySock = -1;
			}

			/*
			* uses thread 0 to pack the items and ships it to CA.
			*/
			if(myId == 0) {
				//		wfaSentStatsResp(gxcSockfd, respBuf);
				printf("respBuf = %s    sent from thr\n", respBuf);
			}
			break;

		case DIRECT_RECV:
#ifdef WFA_WMM_PS_EXT
			if(myProfile->profile == PROF_UAPSD)
			{
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
			break;
#endif /* WFA_WMM_PS_EXT */
		default:
			DPRINT_ERR(WFA_ERR, "Unknown covered case\n");
		}
	}
}

void init_thr_flag()
{
	int cntThr = 0;
	pthread_attr_t ptAttr;
	int ptPolicy;

	struct sched_param ptSchedParam;
	pthread_attr_init(&ptAttr);

	ptSchedParam.sched_priority = 10;
	pthread_attr_setschedparam(&ptAttr, &ptSchedParam);
	pthread_attr_getschedpolicy(&ptAttr, &ptPolicy);
	pthread_attr_setschedpolicy(&ptAttr, SCHED_RR);
	pthread_attr_getschedpolicy(&ptAttr, &ptPolicy);
	/*
	* Create multiple threads for WMM Stream processing.
	*/
	for(cntThr = 0; cntThr< WFA_THREADS_NUM; cntThr++)
	{
		tdata[cntThr].tid = cntThr;
		pthread_mutex_init(&wmm_thr[cntThr].thr_flag_mutex, NULL);
		pthread_cond_init(&wmm_thr[cntThr].thr_flag_cond, NULL);

		wmm_thr[cntThr].thr_id = pthread_create(&wmm_thr[cntThr].thr,
			&ptAttr, wfa_wmm_thread, &tdata[cntThr]);

		sleep(1);
		wmm_thr[cntThr].thr_flag = 0;
	}
}

/*
 * wfaSetThreadPrio():
 *    Set thread priorities
 *    It is an optional experiment if you decide not necessary.
 */
void wfaSetThreadPrio(int tid, int class)
{
	struct sched_param tschedParam;
	pthread_attr_t tattr;
	pthread_attr_init(&tattr);
	pthread_attr_setschedpolicy(&tattr, SCHED_RR);

	switch(class)
	{
	case TG_WMM_AC_BK:
		tschedParam.sched_priority = 70-3;
		break;
	case TG_WMM_AC_VI:
		tschedParam.sched_priority = 70-1;
		break;
	case TG_WMM_AC_VO:
		tschedParam.sched_priority = 70;
		break;
	case TG_WMM_AC_BE:
		tschedParam.sched_priority = 70-2;
	default:
		/* default */
		;
	}

	pthread_attr_setschedparam(&tattr, &tschedParam);
}

int WmmpsTrafficRecv(int sock, char *buf, struct sockaddr *from)
{
	int bytesRecvd;
	socklen_t  addrLen;
	bytesRecvd = recvfrom(sock, buf, MAX_UDP_LEN, 0, from, &addrLen);
	return bytesRecvd;
}
