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

#include <Winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>


#include "wfa_debug.h"
#include "wfa_sock.h"
#include "wfa_main.h"
#include "wfa_types.h"
#include "wfa_tg.h"
#include "wfa_miscs.h"
#include "wfa_cmds.h"

#ifndef SIOCGIWNAME
#define SIOCGIWNAME 0x8B01
#endif

#define RECV_OFFSET 22

extern unsigned short wfa_defined_debug;
extern tgStream_t *theStreams;
extern char PingStr[];
tgStream_t *findStreamProfile(int id);
HANDLE processHandle = NULL;
HANDLE processThread = NULL; 

void
asd_sleep(int SleepTime)
{

	Sleep(SleepTime * 1000);

}

void
uapsd_sleep(int SleepTime)
{

	usleep(SleepTime/1000);

}

void 
asd_closeSocket(int sockfd)
{
	closesocket(sockfd);

}

void
asd_shutDown(int Sockfd)
{

	shutdown(Sockfd, SD_SEND);
}

void 
wfaGetSockOpt(int sockfd, int* tosval, socklen_t* size)
{
	getsockopt(sockfd, IPPROTO_IP, IP_TOS, (char FAR*)tosval, size);
}

int 
wfaSetSockOpt(int sockfd, int* tosval, int size)
{
	int sockOpt;
	sockOpt = setsockopt(sockfd, IPPROTO_IP, IP_TOS, (char FAR*)tosval, size);
	return sockOpt;
}

int
exec_process_cnclient (char *buf, char *rwl_client_path, int rwl_wifi_flag)
{
	unsigned int  position;
	char gCmdStr[WFA_CMD_STR_SZ];
	int timeout_count = 30;
	FILE *tmpfd;
	/* Create Process related variables */
	PROCESS_INFORMATION ProcessInfo;
	char Args[WFA_BUFF_512];
	TCHAR pDefaultCMD[WFA_BUFF_512];
	STARTUPINFO StartupInfo;
	
	if (rwl_wifi_flag){
		/*The server can be in different channel after association
		 *Hence we issue a findserver and then find out if it has
		 *been associated to return the right status
		 */	
	
		memset(Args, 0, WFA_BUFF_512);
		sprintf(Args, "%s", (const char *)buf);
		memset(&ProcessInfo, 0, sizeof(ProcessInfo));
		memset(&StartupInfo, 0, sizeof(StartupInfo));
		StartupInfo.cb = sizeof(StartupInfo);
		/*  "/C" option - Do the command and EXIT the command processor */
		_tcscpy(pDefaultCMD, _T("cmd.exe /C "));
		_tcscat(pDefaultCMD, Args);
		if(!CreateProcess(NULL,(LPTSTR)pDefaultCMD, NULL,NULL,FALSE,FALSE,NULL,NULL,
			&StartupInfo,&ProcessInfo)){
				processHandle = ProcessInfo.hProcess;
				processThread = ProcessInfo.hThread; 
				return FALSE;
		}
		asd_sleep(3);
		sprintf(gCmdStr, "%s findserver", rwl_client_path);
		exec_process(gCmdStr);
	}
	else {
		exec_process(buf);
	}
	
	/* while cnClient associates in the child, parent looks 
	 * for association if it has happened
	 * If it has not associated within a loop of 30, it comes out
	 * as not associated
	 */ 
	while(timeout_count > 0){
		strcpy(gCmdStr, rwl_client_path);
		strcat(gCmdStr, " assoc >");
		if((tmpfd = asd_Config(gCmdStr,TEMP_FILE_PATH)) == NULL){
			DPRINT_ERR(WFA_ERR, "\nassoc failed\n");
			return FALSE;
			}
		if((FileSearch(tmpfd, L"Not associated", &position))== -1) {
			Cleanup_File(tmpfd);
			break;
		}
		Cleanup_File(tmpfd);
		asd_sleep(1);
		timeout_count--;
	}
	if(timeout_count)
		return TRUE;
	else
		return FALSE;
}

void
exec_process(char* command)
{
	char wl_cmd[WFA_BUFF_256];
	memset(wl_cmd,0,sizeof(wl_cmd));
	strncpy(wl_cmd, command, strlen(command));
	DPRINT_INFO(WFA_OUT,"%s\n",wl_cmd);
	asd_Config(wl_cmd," ");
}

int
Start_Socket_Service()
{
	int err = 0;
	WORD wVersionRequested;
	WSADATA wsaData;
	/* Initialize winsock library */
	wVersionRequested = MAKEWORD( 2, 2 );
	err = WSAStartup(wVersionRequested, &wsaData);
	return err;
}

int
Stop_Socket_Service()
{
	return WSACleanup();
}

char * strtok_r(char *s1, const char *s2, char **lasts)
{
	char *ret;

	if (s1 == NULL)
		s1 = *lasts;

	while(*s1 && strchr(s2, *s1))
		++s1;

	if(*s1 == '\0')
		return NULL;

	ret = s1;

	while(*s1 && !strchr(s2, *s1))
		++s1;

	if(*s1)
		*s1++ = '\0';

	*lasts = s1;

	return ret;
} 

int strcasecmp(const char *s1, const char *s2)
{
	const unsigned char *us1 = (const unsigned char *)s1;
	const unsigned char *us2 = (const unsigned char *)s2;

	while (tolower(*us1) == tolower(*us2)) {
		if (*us1++ == '\0')
			return (0);
		us2++;
	}
	return (tolower(*us1) - tolower(*us2));
}

int strncasecmp(const char *s1,const char *s2, unsigned int length)
{
	unsigned char u1, u2;

	for (; length != 0; length--, s1++, s2++) {
		u1 = (unsigned char) *s1;
		u2 = (unsigned char) *s2;

		if (tolower(u1) != tolower(u1)) {
			return tolower(u1) - tolower(u1);
		}
		if (u1 == '\0') {
			return 0;
		}
	}
	return 0;
}

__inline int wmemcmp(const wchar_t *_S1, const wchar_t *_S2, size_t _N)
        {for (; 0 < _N; ++_S1, ++_S2, --_N)
                if (*_S1 != *_S2)
                        return (*_S1 < *_S2 ? -1 : +1);
        return (0); }
__inline wchar_t *wmemcpy(wchar_t *_S1, const wchar_t *_S2, size_t _N)
        {wchar_t *_Su1 = _S1;
        for (; 0 < _N; ++_Su1, ++_S2, --_N)
                *_Su1 = *_S2;
        return (_S1); }
__inline wchar_t *wmemmove(wchar_t *_S1, const wchar_t *_S2, size_t _N)
        {wchar_t *_Su1 = _S1;
        if (_S2 < _Su1 && _Su1 < _S2 + _N)
                for (_Su1 += _N, _S2 += _N; 0 < _N; --_N)
                        *--_Su1 = *--_S2;
        else
                for (; 0 < _N; --_N)
                        *_Su1++ = *_S2++;
        return (_S1); }
__inline wchar_t *wmemset(wchar_t *_S, wchar_t _C, size_t _N)
        {wchar_t *_Su = _S;
        for (; 0 < _N; ++_Su, --_N)
                *_Su = _C;
        return (_S); }

int GetPingStat(FILE* pFile, const wchar_t* lpszSearchString , unsigned int *recv,unsigned int *sent, const char* delim) 
{ 
	unsigned long ulFileSize, ulBufferSize; 
	wchar_t lpBuffer[WFA_BUFF_512];
	char mbsbuffer[WFA_BUFF_512], str[WFA_BUFF_128];
	unsigned int *ulCurrentPosition, *ulReceived, *ulsent;
	int retval;

	ulCurrentPosition=recv;
	ulReceived= recv;
	ulsent = sent; 

	//make sure we were passed a valid, if it isn't return -1 
	if ((!pFile)||(!lpszSearchString)) { 
		return -1; 
	} 

	//get the size of the file 
	fseek(pFile,0,SEEK_END); 

	ulFileSize=ftell(pFile); 

	fseek(pFile,0,SEEK_SET); 

	//if the file is empty return -1 
	if (!ulFileSize) { 
		return -1; 
	} 

	//get the length of the string we're looking for, this is 
	//the size the buffer will need to be 
	ulBufferSize=wcslen(lpszSearchString); 

	if (ulBufferSize>ulFileSize) { 
		return -1; 
	} 

	*ulCurrentPosition=0; 

	//this is where the actual searching will happen, what happens 
	//here is we set the file pointer to the current position 
	//is incrimented by one each pass, then we read the size of 
	//the buffer into the buffer and compare it with the string 
	//we're searching for, if the string is found we return the 
	//position at which it is found 
	while (*ulCurrentPosition<ulFileSize-ulBufferSize) { 
		fseek(pFile,*ulCurrentPosition,SEEK_SET); 
		wmemset(lpBuffer, 0, WFA_BUFF_512);
		//read ulBufferSize bytes from the file 
		fread(mbsbuffer,1,ulBufferSize,pFile); 

		retval = mbstowcs(lpBuffer, mbsbuffer, strlen(mbsbuffer)); 

		//if the data read matches the string we're looking for
		//read from the offset where recieve data starts 
		if (!wmemcmp(lpBuffer, lpszSearchString, ulBufferSize)) { 
			fread(str, 1, RECV_OFFSET, pFile);
			*ulReceived = atoi(str);
			strtok(str, (const char*)ulReceived);
			//fread(str,1,ulBufferSize,pFile);
			//strtok(str, delim);
			*ulsent= atoi(str);
			//return the position the string was found at 
			return *ulCurrentPosition; 
		} 

		++*ulCurrentPosition; 
	} 
	return -1;
} 

int GetStats(FILE* pFile, const wchar_t* lpszSearchString , unsigned int *pos, const char* delim) 
{ 

	char* lpBuffer;
	char* str;
    char mbsString[128];
	unsigned long ulFileSize, ulBufferSize; 	
	unsigned int *ulCurrentPosition;
	ulCurrentPosition = pos;

	memset(mbsString, 0, 128);
	/* convert the wide character search string to multibyte */
	wcstombs(mbsString,lpszSearchString, wcslen(lpszSearchString) );
	/* make sure we were passed a valid, if it isn't return -1 */
	if ((!pFile)||(!lpszSearchString)) { 
		return -1; 
	} 

	/* get the size of the file */
	fseek(pFile,0,SEEK_END); 

	ulFileSize=ftell(pFile); 

	fseek(pFile,0,SEEK_SET); 

	/* if the file is empty return -1 */
	if (!ulFileSize) { 
		return -1; 
	} 

	/* get the length of the string we're looking for, this is 
	 * the size the buffer will need to be */
	ulBufferSize=wcslen(lpszSearchString); 

	if (ulBufferSize>ulFileSize) { 
		return -1; 
	} 
    /* allocate the memory for the buffer */
     lpBuffer=(char*)malloc(strlen(mbsString)); 
	 str = (char*)malloc(strlen(mbsString));
	/* if malloc() returned a null pointer (which probably means 
     * there is not enough memory) then return -1  */
	 if (!lpBuffer ) 
    { 
        return -1; 
    } 

	*ulCurrentPosition=0; 

	/* this is where the actual searching will happen, what happens 
	 * here is we set the file pointer to the current position 
	 * is incrimented by one each pass, then we read the size of 
	 * the buffer into the buffer and compare it with the string 
	 * we're searching for, if the string is found we return the 
	 * position at which it is found */

	while (*ulCurrentPosition<ulFileSize-ulBufferSize) { 
		fseek(pFile,*ulCurrentPosition,SEEK_SET); 
		/* if the data read matches the string we're looking for */
		fread(lpBuffer,1,ulBufferSize,pFile); 
		if (!memcmp(lpBuffer,mbsString,ulBufferSize)) 
        { 
			fread(str,1,ulBufferSize,pFile);
			strtok(str, delim);
			*ulCurrentPosition = atoi(str);

			/* free the buffer */
			if(lpBuffer != NULL || str != NULL){
				free(lpBuffer); 
				free(str);
			}
			/* return the position the string was found at */
			return *ulCurrentPosition; 
		} 
        /* incriment the current position by one */
		++*ulCurrentPosition; 
    } 

    /* if we made it this far the string was not found in the file 
     * so we free the buffer */
    if(lpBuffer != NULL || str != NULL){
			free(str);
            free(lpBuffer); 
	} 
	return -1;
} 

int FileSearch(FILE* pFile, const wchar_t* lpszSearchString , unsigned int *pos) 
{ 
	
	char* lpBuffer;
	 char mbsString[128];
	unsigned long ulFileSize = 0, ulBufferSize;
	unsigned long *ulCurrentPosition;
	ulCurrentPosition= pos;
	memset(mbsString, 0, 128);
	/* convert the wide character search string to multibyte */
	wcstombs(mbsString,lpszSearchString, wcslen(lpszSearchString) );
	/* make sure we were passed a valid, if it isn't return -1 */
	if ((!pFile)||(!lpszSearchString)) { 
		return -1; 
	} 

	/* get the size of the file */
	fseek(pFile,0,SEEK_END); 

	ulFileSize=ftell(pFile); 

	fseek(pFile,0,SEEK_SET); 

	/* if the file is empty return -1 */
	if (!ulFileSize) { 
		return -1; 
	} 

	/* get the length of the string we're looking for, this is 
	 * the size the buffer will need to be */
	ulBufferSize=wcslen(lpszSearchString); 

	if (ulBufferSize > ulFileSize) { 
		return -1; 
	} 

    /* allocate the memory for the buffer */
     lpBuffer=(char*)malloc(strlen(mbsString)); 
	/* if malloc() returned a null pointer (which probably means 
     * there is not enough memory) then return -1 */

	 if (!lpBuffer ) 
    { 
        return -1; 
    } 

	*ulCurrentPosition=0; 

	/* this is where the actual searching will happen, what happens 
	 * here is we set the file pointer to the current position 
	 * is incrimented by one each pass, then we read the size of 
	 * the buffer into the buffer and compare it with the string 
	 * we're searching for, if the string is found we return the 
	 * position at which it is found */
	while (*ulCurrentPosition<ulFileSize-ulBufferSize) { 
		fseek(pFile,*ulCurrentPosition,SEEK_SET); 
		/* if the data read matches the string we're looking for */
		  fread(lpBuffer,1,ulBufferSize,pFile); 
		if (!memcmp(lpBuffer,mbsString,ulBufferSize)) 
		{ 
			/* free the buffer */
			if(lpBuffer != NULL)
            free(lpBuffer); 
			/* return the position the string was found at */
			return *ulCurrentPosition; 
		} 

		/* incriment the current position by one */
		++*ulCurrentPosition; 
	} 
    /* if we made it this far the string was not
     * found in the file so we free the buffer */
    if(lpBuffer != NULL)
            free(lpBuffer); 
	return -1;
} 

unsigned int uGetLocalIP()
{
	char szHostName[WFA_BUFF_256];
	struct hostent*	HostData;

	GetHostName(szHostName, WFA_BUFF_256);
	HostData = gethostbyname(szHostName);
	if (HostData == NULL)
		return 0;

	return *((unsigned int*)HostData->h_addr);
}

char* GetHostName(char* buf, int len) 
{
	if (gethostname(buf, len) == SOCKET_ERROR)
		return NULL;
	return buf;
}

int wfa_Winitime_diff(SYSTEMTIME *t1, SYSTEMTIME *t2)
{
	int dtime;
	int sec = t2->wSecond - t1->wSecond;
	int msec = (t2->wMilliseconds - t1->wMilliseconds);

	if(msec < 0)
	{
		sec -=1;
		msec += 1000;
	}

	dtime = sec*1000 + msec;
	return dtime;
}

int wfa_Win_estimate_timer_latency()
{
	SYSTEMTIME t1, t2, tp2;
	int sleep =2;
	int latency =0;

	GetSystemTime(&t1);
	usleep(sleep);

	GetSystemTime(&t2); 

	tp2.wMilliseconds = t1.wMilliseconds  +20;
	if( tp2.wMilliseconds >= 1000) {
		tp2.wSecond = t1.wSecond +1;
		tp2.wMilliseconds -= 1000;
	}
	else
		tp2.wSecond = t1.wSecond;

	return latency = (t2.wSecond - tp2.wSecond)  + (t2.wMilliseconds - tp2.wMilliseconds); 
}


FILE* asd_Config(char *strFunct, char *strstrParams)
{
	FILE* fp = NULL;
	PROCESS_INFORMATION ProcessInfo;
	char Args[WFA_BUFF_512];
	TCHAR pDefaultCMD[WFA_BUFF_512];
	STARTUPINFO StartupInfo;
	ULONG rc;

	Args[0]= 0;
	memset(Args, 0, WFA_BUFF_512);
	sprintf(Args, "%s %s", (const char *)strFunct, (const char *)strstrParams);
	memset(&ProcessInfo, 0, sizeof(ProcessInfo));
	memset(&StartupInfo, 0, sizeof(StartupInfo));
	StartupInfo.cb = sizeof(StartupInfo);
	/*  "/C" option - Do the command and EXIT the command processor */
	_tcscpy(pDefaultCMD, _T("cmd.exe /C "));
	_tcscat(pDefaultCMD, Args);

	if(!CreateProcess(NULL,(LPTSTR)pDefaultCMD, NULL,NULL,FALSE,FALSE,NULL,NULL,&StartupInfo,&ProcessInfo))
	{
		processHandle = ProcessInfo.hProcess;
		processThread = ProcessInfo.hThread; 
		printf("CreateProcess function failed\n");
		return NULL;
	}

	WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
	if(!GetExitCodeProcess(ProcessInfo.hProcess, &rc))
		rc = 0;

	CloseHandle(ProcessInfo.hThread);
	CloseHandle(ProcessInfo.hProcess);
	if(strlen(strstrParams) > 1){
		if((fp = fopen(TEMP_FILE_PATH, "r+")) == NULL){
			printf("fopen failed\n");
			return NULL;
		}
	}
	return fp;
}


void Cleanup_File(FILE* fp)
{
	fclose(fp);
	if (!DeleteFile(TEMP_FILE_PATH)) {
		printf("DeleteFile Failed with error:%d\n",GetLastError());
	}
}

void p_error(char *errorString)
{
	printf("%s %d",errorString, GetLastError());
}
