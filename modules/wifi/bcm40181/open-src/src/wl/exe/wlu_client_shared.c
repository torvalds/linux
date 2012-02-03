/*
 * Common code for remotewl client
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlu_client_shared.c,v 1.24.2.1 2010/12/16 01:58:34 Exp $
 */
#ifdef WIN32
#define NEED_IR_TYPES

#include <winsock2.h>
#include <windows.h>
#include <ntddndis.h>
#include <epictrl.h>
#include <irelay.h>
#include <winioctl.h>
#include <nuiouser.h>
#else /* LINUX */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/wait.h>
#endif /* WIN32 */
#include <stdio.h>
#include <stdlib.h>

#include <malloc.h>

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <typedefs.h>
#include <wlioctl.h>
#include <proto/ethernet.h>
#include <bcmendian.h>
#include <bcmutils.h>
#include <bcmcdc.h>
#include <proto/802.11.h>
#include <signal.h>
#if defined(RWL_WIFI) || defined(WIFI_REFLECTOR)
#include <rwl_wifi.h>
#endif /* defined(RWL_WIFI) || defined(WIFI_REFLECTOR) */
#include "wlu.h"
#include "wlu_remote.h"
#include "wlu_pipe.h"
#include "wlu_client_shared.h"

char* remote_vista_cmds[] = {"join", "sup_wpa", "wsec", "set_pmk", "legacylink", "list",
	"disassoc", "xlist", NULL};
int vista_cmd_index;
#define SHELL_CMD_LEN (256)

#ifdef RWL_SOCKET
extern int validate_server_address();
static int rwl_socket_shellresp(void *wl, rem_ioctl_t *rem_ptr, uchar *input_buf);
#endif
static int rwl_dongle_shellresp(void *wl, rem_ioctl_t *rem_ptr, uchar *input_buf, int cmd);
#ifdef RWL_WIFI
static int rwl_wifi_shellresp(void *wl, rem_ioctl_t *rem_ptr, uchar *input_buf);
#endif
/* We don't want the server to allocate bigger buffers for some of the commands
 * like scanresults. Server can still allocate 8K memory and send the response
 * in fragments. This is used in case of Get commands only.
 */
#define SERVER_RESPONSE_MAX_BUF_LEN 8192

extern unsigned short g_rwl_servport;
extern char *g_rwl_servIP;
int g_child_pid;
#ifdef RWL_WIFI
/* dword align allocation */
static union {
	char bufdata[WLC_IOCTL_MAXLEN];
	uint32 alignme;
} bufstruct_wlu;
static char *g_rwl_aligned_buf = (char*) &bufstruct_wlu.bufdata;

extern char *g_rwl_buf_mac;
#endif
#ifdef RWL_SOCKET
/* Make initial connection from client to server through sockets */
static int
rwl_connect_socket_server(void)
{
	int SockDes = -1;
	struct sockaddr_in servAddr;
	memset(&servAddr, 0, sizeof(servAddr));
	if ((SockDes = (*(int *)rwl_open_pipe(remote_type, "\0", 0, 0))) == FAIL)
		return FAIL;
	if (!validate_server_address())
		return FAIL;
	DPRINT_DBG(OUTPUT, "ServerIP:%s,ServerPort:%d\n", g_rwl_servIP, g_rwl_servport);
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = hton16(g_rwl_servport);
	servAddr.sin_addr.s_addr = inet_addr(g_rwl_servIP);

	if (rwl_sockconnect(SockDes, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
		rwl_close_pipe(remote_type, (void*) &SockDes);
		return FAIL;
	}
	return SockDes;
}

/* This routine is used for both Get and Set Ioctls for the socket */
static int
rwl_information_socket(void *wl, int cmd, void* input_buf, unsigned long *input_len,
	unsigned long *tx_len, uint flags)
{
	int error, Sockfd;
	rem_ioctl_t *rem_ptr = NULL;

	if ((Sockfd = rwl_connect_socket_server()) < 0) {
		DPRINT_ERR(ERR, "Error in getting the Socket Descriptor\n");
		return FAIL;
	}
	wl = (void *)(&Sockfd);

	if (remote_CDC_tx(wl, cmd, input_buf, *input_len,
		*tx_len, flags, 0) < 0) {
		DPRINT_ERR(ERR, "query_info_fe: Send command failed\n");
		rwl_close_pipe(remote_type, wl);
		return FAIL;
	}

	if ((rem_ptr = remote_CDC_rx_hdr(wl, 0)) == NULL) {
		DPRINT_ERR(ERR, "query_info_fe: Reading CDC header 	failed\n");
		rwl_close_pipe(remote_type, wl);
		return FAIL;
	}
	rwl_swap_header(rem_ptr, NETWORK_TO_HOST);

	if (rem_ptr->msg.len > *input_len) {
		DPRINT_ERR(ERR, "query_info_fe: needed size(%d) > "
		           "actual size(%ld)\n", rem_ptr->msg.len, *input_len);
		rwl_close_pipe(remote_type, wl);
		return FAIL;
	}

	if (remote_CDC_rx(wl, rem_ptr, input_buf, *input_len, 0) == FAIL) {
		DPRINT_ERR(ERR, "query_info_fe: No results!\n");
		rwl_close_pipe(remote_type, wl);
		return FAIL;
	}

	if (rem_ptr->msg.flags & REMOTE_REPLY)
		error = rem_ptr->msg.cmd;
	else
		error = 0;

	rwl_close_pipe(remote_type, wl);

	return error;
}
#endif /* RWL_SOCKET */
/*
 * Receives the fragmented response from serial server.
 * Called from rwl_queryinformation_fe front end function for dongle case only when
 * the client expects data in chunks more than DATA_FRAME_LEN * (960) bytes.
 */
static int
rwl_serial_fragmented_response_fe(void *wl, rem_ioctl_t *rem_ptr, void* input_buf,
unsigned long *bytes_to_read)
{
	int error;
	uint32 total_numread;
	uchar *local_buf, *local_result;
	uint frames_to_read, frame_count;

	DPRINT_DBG(OUTPUT, "rem_ptr->msg.len=%d \t rem_ptr->data_len=%d\n",
		rem_ptr->msg.len, rem_ptr->data_len);

	/* Calculate the number of frames for the response */
	if (rem_ptr->data_len == 0) {
		DPRINT_ERR(ERR, "data_len is:%d\n", rem_ptr->data_len);
		return (FAIL);
	}
	frames_to_read = (*bytes_to_read)/rem_ptr->data_len;
	if ((*bytes_to_read) % rem_ptr->data_len > 0)
		frames_to_read += 1;

	DPRINT_DBG(OUTPUT, "No of frames=%d\n", frames_to_read);

	if ((local_result = (uchar*)malloc(rem_ptr->msg.len)) == NULL) {
		DPRINT_ERR(ERR, "Malloc failed for serial fragmented frame"
		"local result \n");
		return FAIL;
	}

	/* Response will come in DATA_FRAME_LEN + REMOTE_SIZE (960+16) bytes 
	 * packet with CDC header and then followed by response.
	 */
	total_numread = 0;
	frame_count = 0;
	while (total_numread != *bytes_to_read) {
		local_buf = (uchar*)malloc(rem_ptr->data_len);
		if (local_buf == NULL) {
			free(local_result);
			return FAIL;
		}

		DPRINT_DBG(OUTPUT, "Total bytes=%d\n", total_numread);
		DPRINT_DBG(OUTPUT, "Frame (Reverse):%d\n", frames_to_read);

		if (remote_CDC_rx(wl, rem_ptr, local_buf, rem_ptr->data_len, 0) == -1) {
			free(local_buf);
			free(local_result);
			return FAIL;
		}

		/* Concatenate the response to loc_res. */
		memcpy(&local_result[frame_count*DATA_FRAME_LEN], local_buf,
			rem_ptr->data_len);

		total_numread += rem_ptr->data_len;
		frame_count++;
		frames_to_read--;

		DPRINT_DBG(OUTPUT, "Total bytes=%d\n", total_numread);

		if (frames_to_read == 0) {
			/* When all the frames are received copy the data to original buf */
			memcpy(input_buf, local_result, total_numread);
			if (rem_ptr->msg.flags & REMOTE_REPLY) {
				error = rem_ptr->msg.cmd;
			}
		}

		if (total_numread != *bytes_to_read) {
			/* Receive the next header */
			if ((rem_ptr = remote_CDC_rx_hdr(wl, 0)) == NULL) {
				DPRINT_ERR(ERR, "query_info_fe: Reading CDC header failed");
			}
		}
		free(local_buf);
	}
	free(local_result);
	return error;
}

/* This routine is common to both set and Get Ioctls for the dongle case */
static int
rwl_information_dongle(void *wl, int cmd, void* input_buf, unsigned long *input_len,
	uint tx_len, uint flags)
{
	int error;
	rem_ioctl_t *rem_ptr = NULL;

	if (remote_CDC_tx(wl, cmd, input_buf, *input_len,
		tx_len, flags, 0) < 0) {
		DPRINT_ERR(ERR, "query_info_fe: Send command failed\n");
		return FAIL;
	}

	/* Get the header */
	if ((rem_ptr = remote_CDC_rx_hdr(wl, 0)) == NULL) {
		DPRINT_ERR(ERR, "query_info_fe: Reading CDC header failed\n");
		return SERIAL_PORT_ERR;
	}

	/* Make sure there is enough room */
	if (rem_ptr->msg.len > *input_len) {
		DPRINT_DBG(OUTPUT, "query_info_fe: needed size(%d) > actual"
			"size(%ld)\n", rem_ptr->msg.len, *input_len);
		return FAIL;
	}

	/*  We can grab short frames all at once.  Longer frames (> 960 bytes) 
	 *  come in fragments.
	*/
	if (rem_ptr->data_len < DATA_FRAME_LEN) {
		if (remote_CDC_rx(wl, rem_ptr, input_buf, *input_len, 0) == FAIL) {
			DPRINT_ERR(ERR, "query_info_fe: No results!\n");
			return FAIL;
		}
		if (rem_ptr->msg.flags & REMOTE_REPLY)
			error = rem_ptr->msg.cmd;
		else
			error = 0;
	} else {
		/* rwl_serial_fragmented_response_fe returns either valid number or FAIL. 
		  * In any case return the same value to caller
		*/
		error = rwl_serial_fragmented_response_fe(wl, rem_ptr, input_buf, input_len);
	}
	return error;
}
/* Handler to signal the reader thread that ctrl-C is pressed */
volatile sig_atomic_t g_sig_ctrlc = 1;
void ctrlc_handler(int num)
{
	UNUSED_PARAMETER(num);
	g_sig_ctrlc = 0;
}

/* Issue shell commands independent of transport type and return result */
static int
rwl_shell_information_fe(void *wl, int cmd, uchar* input_buf, unsigned long *input_len)
{
	int error, remote_cmd;
	uchar* resp_buf = NULL;
	rem_ioctl_t *rem_ptr = NULL;

#ifdef RWL_WIFI
	char *cbuf, retry;
	dot11_action_wifi_vendor_specific_t *list;
#endif

#ifdef RWL_SOCKET
	int Sockfd;
#endif

	remote_cmd = REMOTE_SHELL_CMD;

#ifdef RWLASD
	if (cmd == ASD_CMD)
		remote_cmd = REMOTE_ASD_CMD;
#endif
	if (cmd == VISTA_CMD)
		remote_cmd = REMOTE_VISTA_CMD;

	switch (remote_type) {
		case REMOTE_SOCKET:
#ifdef RWL_SOCKET
			if ((Sockfd = rwl_connect_socket_server()) < 0) {
				DPRINT_ERR(ERR, " Error in getting the SocDes\n");
				return BCME_ERROR;
			}

			wl = (void *)(&Sockfd);
			if (remote_CDC_tx(wl, cmd, input_buf, *input_len, *input_len,
				remote_cmd, 0) < 0) {
				DPRINT_ERR(ERR, "shell_info_fe: Send command failed\n");
				rwl_close_pipe(remote_type, wl);
				return BCME_ERROR;
			}
			/* For backward compatibility for ASD, async and kill commands do the 
			 * old way
			 */
			if (remote_cmd == REMOTE_SHELL_CMD && !strstr((char*)input_buf, "%") &&
				!strstr((char*)input_buf, "kill"))
				error = rwl_socket_shellresp(wl, rem_ptr, input_buf);
			else {
				if ((rem_ptr = remote_CDC_rx_hdr(wl, 0)) == NULL) {
					DPRINT_ERR(ERR, "shell_info_fe: Receiving CDC"
						"header failed\n");
					rwl_close_pipe(remote_type, wl);
					return FAIL;
				}

				if ((resp_buf = malloc(rem_ptr->msg.len + 1)) == NULL) {
					DPRINT_ERR(ERR, "Mem alloc fails\n");
					rwl_close_pipe(remote_type, wl);
					return FAIL;
				}

				if (remote_CDC_rx(wl, rem_ptr, resp_buf,
					rem_ptr->msg.len, 0) == FAIL) {
					DPRINT_ERR(ERR, "shell_info_fe: No results!\n");
					rwl_close_pipe(remote_type, wl);
					free(resp_buf);
					return FAIL;
				}

				/* print the shell result */
				resp_buf[rem_ptr->msg.len] = '\0';
				/* The return value of the shell command 
				 * will be stored in rem_ptr->msg.cmd
				 * Return that value to the client process
				 */
				if (rem_ptr->msg.flags & REMOTE_REPLY)
					error = rem_ptr->msg.cmd;
				fputs((char*)resp_buf, stdout);
			}
			rwl_close_pipe(remote_type, wl);
#endif /* RWL_SOCKET */
			break;
		case REMOTE_DONGLE:
		case REMOTE_SERIAL:
			if (remote_CDC_tx(wl, cmd, input_buf, *input_len, *input_len,
			                  remote_cmd, 0) < 0) {
				DPRINT_ERR(ERR, "shell_info_fe: Send command failed\n");
				return FAIL;
			}

			/* For backward compatibility for ASD, async and kill commands do the 
			 * old way
			 */
			if (remote_cmd != REMOTE_ASD_CMD && !strstr((char*)input_buf, "%") &&
				!strstr((char*)input_buf, "kill"))
				error = rwl_dongle_shellresp(wl, rem_ptr, input_buf, cmd);
			else {
				if ((rem_ptr = remote_CDC_rx_hdr(wl, 0)) == NULL) {
					DPRINT_ERR(ERR, "shell_info_fe:"
					"Receiving CDC header failed\n");
					return SERIAL_PORT_ERR;
				}
			rwl_swap_header(rem_ptr, NETWORK_TO_HOST);
			/* In case of shell or ASD commands the response 
			 * size is not known in advance
			 * Hence based on response from the server memory is allocated
			 */
			if ((resp_buf = malloc(rem_ptr->msg.len + 1)) == NULL) {
				DPRINT_ERR(ERR, "Mem alloc fails for shell response buffer\n");
				return FAIL;
			}

			if (rem_ptr->data_len < DATA_FRAME_LEN) {
				/* Response comes in one shot not in fragments */
				if (remote_CDC_rx(wl, rem_ptr, resp_buf,
					rem_ptr->msg.len, 0) == FAIL) {
					DPRINT_ERR(ERR, "shell_info_fe: No results!\n");
					free(resp_buf);
					return FAIL;
				}
			} else {
				error = rwl_serial_fragmented_response_fe(wl, rem_ptr,
					resp_buf, (unsigned long *)&(rem_ptr->msg.len));
			}
			/* print the shell result */
			resp_buf[rem_ptr->msg.len] = '\0';
			/* The return value of the shell command will be stored in rem_ptr->msg.cmd
			 * Return that value to the client process
			 */
				if (rem_ptr->msg.flags & REMOTE_REPLY)
					error = rem_ptr->msg.cmd;
			fputs((char*)resp_buf, stdout);
			}
			break;

		case REMOTE_WIFI:
#ifdef RWL_WIFI
			/* Unlike dongle or UART case the response for wi-fi comes in single frame.
			 * (CDC header + data). Hence the single read is called for header and data.
			 * If any error in reading then we sleep for some time before retrying.
			 */
			if ((list = (dot11_action_wifi_vendor_specific_t *)
				malloc(RWL_WIFI_ACTION_FRAME_SIZE)) == NULL) {
				return FAIL;
			}
			if ((rem_ptr = (rem_ioctl_t *)malloc(REMOTE_SIZE)) == NULL) {
				free(list);
				return FAIL;
			}

			if ((cbuf = (char*) malloc(*input_len)) == NULL) {
				DPRINT_ERR(ERR, "Malloc failed for shell response\n");
				free(rem_ptr);
				free(list);
				return FAIL;
			}

			/* copy of the original buf is required for retry */
			memcpy(cbuf, (char*)input_buf, *input_len);
			for (retry = 0; retry < RWL_WIFI_RETRY; retry++) {

				rwl_wifi_purge_actionframes(wl);
				if (remote_CDC_tx(wl, cmd, input_buf, *input_len, *input_len,
				remote_cmd, 0) < 0) {
						DPRINT_DBG(OUTPUT, "rwl_shell_information_fe(wifi):"
							"Send command failed\n");
						rwl_sleep(RWL_WIFI_RETRY_DELAY);
						free(rem_ptr);
						free(list);
						return FAIL;
				}
				/* For backward compatibility for ASD, 
				 * async and kill commands do the
				 * old way
				 */
				if (remote_cmd == REMOTE_SHELL_CMD &&
					!strstr((char*)input_buf, "%") &&
					!strstr((char*)input_buf, "kill")) {
					error = rwl_wifi_shellresp(wl, rem_ptr, input_buf);
						if (rem_ptr->msg.len == 0)
						break;
					}
				else if (remote_cmd == REMOTE_VISTA_CMD) {
					if ((error = remote_CDC_DATA_wifi_rx_frag(wl, rem_ptr, 0,
					NULL, RWL_WIFI_SHELL_CMD)) < 0) {
						DPRINT_DBG(OUTPUT, "rwl_shell_information_fe(wifi):"
						"error in reading shell response\n");
						continue;
					}
					if (rem_ptr->msg.flags & REMOTE_REPLY) {
						 error = rem_ptr->msg.cmd;
						 break;
					} else {
						rwl_sleep(RWL_WIFI_RETRY_DELAY);
						continue;
					}
				}
				else {
					/* ASD commands may take long time to give back
					* the response (eg: file transfer)
					*/
					if (remote_cmd == REMOTE_ASD_CMD) {
						for (;;) {
						/* copy back the buffer to input buffer */
						memcpy((char*)input_buf, cbuf, *input_len);

							if ((error = remote_CDC_DATA_wifi_rx_frag(
								wl, rem_ptr, 0, NULL,
								RWL_WIFI_SHELL_CMD)) < 0) {
								DPRINT_DBG(OUTPUT,
								"rwl_shell_information_fe(wifi):"
								"err in reading shell response\n");
								continue;
							}
							if (rem_ptr->msg.flags & REMOTE_REPLY) {
								error = rem_ptr->msg.cmd;
								retry = RWL_WIFI_RETRY;
								break;
							} else {
								rwl_sleep(RWL_WIFI_RETRY_DELAY);
								continue;
							}
						}
					}
				}

			}
			free(rem_ptr);
			free(list);
			if (cbuf != NULL)
				free(cbuf);
#endif /* RWL_WIFI */
			break;

		default:
			break;
	} /* End of switch (remote_type) */

	if (resp_buf)
		free(resp_buf);

	return error;
}

/* Prepare to issue shell command */
int
rwl_shell_cmd_proc(void *wl, char **argv, int cmd)
{
	uchar *buff;
	unsigned long len = SHELL_CMD_LEN;
	int err;

	if ((buff = malloc(SHELL_CMD_LEN)) == NULL) {
		DPRINT_ERR(ERR, "\n Mem alloc fails for shell cmd buffer\n");
		return FAIL;
	}

	memset(buff, 0, sizeof(SHELL_CMD_LEN));
	while (*argv) {
		strcat((char*)buff, *argv);
		argv++;
		if (*argv)
			strcat((char*)buff, " "); /* leave space between args */
	}

	err = rwl_shell_information_fe(wl, cmd, buff, &len);
	free(buff);
	return err;
}

/* transport independent entry point for GET ioctls */
int
rwl_queryinformation_fe(void *wl, int cmd, void* input_buf,
	unsigned long *input_len, int debug, int rem_ioctl_select) {
	int error = 0;
	uint tx_len;
#if defined(RWL_SERIAL) || RWL_WIFI
	rem_ioctl_t *rem_ptr = NULL;
#endif

#ifdef RWL_WIFI
	int retry;
	char *cinput_buf;
#endif

	UNUSED_PARAMETER(debug);

	switch (remote_type) {
		case REMOTE_SOCKET:
#ifdef RWL_SOCKET
		/* We don't want the server to allocate bigger buffers
		 * for some of the commands
		 * like scanresults. Server can still allocate 8K memory
		 * and send the response
		 */
		if (*input_len > SERVER_RESPONSE_MAX_BUF_LEN)
			*input_len = SERVER_RESPONSE_MAX_BUF_LEN;
		error = rwl_information_socket(wl, cmd, input_buf, input_len,
			input_len, rem_ioctl_select);
#endif /* RWL_SOCKET */
		break;

#ifdef RWL_SERIAL
		/* System serial transport is not supported in Linux. Only XP */
		case REMOTE_SERIAL:
#ifdef SERDOWNLOAD
			tx_len = MIN(*input_len, 512);
#else
			tx_len = MIN(*input_len, 1024);
#endif
			/* Time estimate assumes 115K baud */
			if (*input_len > (1024 *10))
				/* Multiply by 2. The buffer has to go there and back */
				DPRINT_DBG(OUTPUT, "Wait time: %lu seconds\n",
				((*input_len)+ tx_len) / (115200/8));

			if (remote_CDC_tx(wl, cmd, input_buf, *input_len,
			tx_len, rem_ioctl_select, debug) < 0) {
				DPRINT_ERR(ERR, "query_info_fe: Send command failed\n");
				return FAIL;
			}

			if ((rem_ptr = remote_CDC_rx_hdr(wl, debug)) == NULL) {
				DPRINT_ERR(ERR, "query_info_fe: Reading CDC header failed\n");
				return FAIL;
			}

			if (rem_ptr->msg.flags != REMOTE_REPLY) {
				DPRINT_ERR(ERR, "query_info_fe: response format error.\n");
				return FAIL;
			}
			if (rem_ptr->msg.len > *input_len) {
				DPRINT_ERR(ERR, "query_info_fe: needed size(%d) greater than "
				"actual size(%lu)\n", rem_ptr->msg.len, *input_len);
				return FAIL;
			}

			error = rem_ptr->msg.cmd;
			if (error != 0)
				DPRINT_ERR(ERR, "query_info_fe: remote cdc header return "
					   "error code %d\n", error);
			if (remote_CDC_rx(wl, rem_ptr, input_buf, *input_len, debug) == -1) {
				DPRINT_ERR(ERR, "query_info_fe: No results!\n");
				return FAIL;
			}
			if (rem_ptr->msg.flags & REMOTE_REPLY)
				error = rem_ptr->msg.cmd;
			break;

#endif /* RWL_SERIAL */
		case REMOTE_DONGLE:
			/* We don't want the server to allocate bigger buffers
			 * for some of the commands
			 * like scanresults. Server can still allocate 8K
			 *memory and send the response
			 * in fragments.
			 */
			if (*input_len > SERVER_RESPONSE_MAX_BUF_LEN)
				*input_len = SERVER_RESPONSE_MAX_BUF_LEN;

			/* Actual buffer to be sent should be max 256 bytes as
			 *UART input buffer
			 * is 512 bytes
			 */
			tx_len = MIN(*input_len, 256);
			error = rwl_information_dongle(wl, cmd, input_buf, input_len,
				tx_len, rem_ioctl_select);

			break;

		case REMOTE_WIFI:
#ifdef RWL_WIFI
			/* We don't want the server to allocate bigger buffers
			 * for some of the commands
			 * like scanresults. Server can still allocate 8K memory
			 * and send the response
			 * in fragments.
			 */
			if (*input_len > SERVER_RESPONSE_MAX_BUF_LEN)
				*input_len = SERVER_RESPONSE_MAX_BUF_LEN;

			/* Actual buffer to be sent should be max 960 bytes
			 * as wifi max frame size if 960
			 * and actual data for any command will not exceed 960 bytes
			*/
			tx_len = MIN(*input_len, RWL_WIFI_FRAG_DATA_SIZE);

			if ((rem_ptr = (rem_ioctl_t *)malloc(REMOTE_SIZE)) == NULL) {
				return FAIL;
			}
			if ((cinput_buf = (char*)malloc(tx_len)) == NULL) {
				DPRINT_ERR(ERR, "Malloc failed for query information fe"
				"character buf \n");
				free(rem_ptr);
				return FAIL;
			}

			memcpy(cinput_buf, (char*)input_buf, tx_len); /* Keep a copy of input_buf */

			for (retry = 0; retry < RWL_WIFI_RETRY; retry++) {

				rwl_wifi_purge_actionframes(wl);

				if (retry > 3)
					DPRINT_INFO(OUTPUT, "ir_queryinformation_fe : retry %d"
					"cmd %d\n", retry, cmd);

				/* copy back the buffer to input buffer */
				memcpy((char*)input_buf, cinput_buf, tx_len);

				/* Issue the command */
				if ((error = remote_CDC_wifi_tx(wl, cmd, input_buf,
					*input_len, tx_len, rem_ioctl_select)) < 0) {
					DPRINT_DBG(OUTPUT, "query_info_fe: Send command failed\n");
						rwl_sleep(RWL_WIFI_RETRY_DELAY);
						continue;
				}

				if ((error = remote_CDC_DATA_wifi_rx_frag(wl, rem_ptr,
					*input_len, input_buf, RWL_WIFI_WL_CMD) < 0)) {
					DPRINT_DBG(OUTPUT, "ir_queryinformation_fe :"
					"Error in reading the frag bytes\n");
					rwl_sleep(RWL_WIFI_RETRY_DELAY);
					continue;
				}

				if (rem_ptr->msg.flags & REMOTE_REPLY) {
					error = rem_ptr->msg.cmd;
					break;
				} else {
					rwl_sleep(RWL_WIFI_RETRY_DELAY);
				}
			}

			free(rem_ptr);
			if (cinput_buf)
				free(cinput_buf);
			break;
#endif /* RWL_WIFI */
		default:
			DPRINT_ERR(ERR, "rwl_queryinformation_fe: Unknown"
				"remote_type %d\n", remote_type);
		break;
	}
	return error;
}

/*
* This is the front end query function for Set Ioctls. This is used by clients
for executing Set Ioctls.
*/
int
rwl_setinformation_fe(void *wl, int cmd, void* buf,
	unsigned long *len, int debug, int rem_ioctl_select) {
	int error = 0;
	uint tx_len;
#if defined(RWL_SERIAL) || RWL_WIFI
	rem_ioctl_t *rem_ptr = NULL;
#endif

#ifdef RWL_WIFI
	dot11_action_wifi_vendor_specific_t *list = NULL;
	char *cbuf, retry;
#endif

	UNUSED_PARAMETER(debug);

	switch (remote_type) {
		case REMOTE_SOCKET:
#ifdef RWL_SOCKET
			error = rwl_information_socket(wl, cmd, buf, len, len, rem_ioctl_select);
#endif
			break;
#ifdef RWL_SERIAL
		case REMOTE_SERIAL:
			if (*len > (1024 *10))
				DPRINT_DBG(OUTPUT, "Wait time: %lu seconds\n", (*len)/(115200/8));

			if (remote_CDC_tx(wl, cmd, buf, *len, *len, rem_ioctl_select, debug) < 0) {
				DPRINT_ERR(ERR, "set_info_fe: Send command failed\n");
				return FAIL;
			}

			if ((rem_ptr = remote_CDC_rx_hdr(wl, debug)) == NULL) {
				DPRINT_ERR(ERR, "set_info_fe: Reading CDC header failed\n");
				return FAIL;
			}

			if (rem_ptr->msg.flags != REMOTE_REPLY)	{
				DPRINT_ERR(ERR, "set_info_fe: response format error.\n");
				return FAIL;
			}

			if (rem_ptr->msg.len > *len) {
				DPRINT_ERR(ERR, "set_info_fe: needed size (%d) greater than "
					   "actual size (%lu)\n", rem_ptr->msg.len, *len);
				return FAIL;
			}

			error = rem_ptr->msg.cmd;
			if (error != 0) {
				DPRINT_ERR(ERR, "set_info_fe: remote cdc header return "
					   "error code (%d)\n", error);
			}
			if (remote_CDC_rx(wl, rem_ptr, buf, rem_ptr->msg.len, debug) == -1) {
				DPRINT_ERR(ERR, "set_info_fe: fetching results failed\n");
				return FAIL;
			}

			if (rem_ptr->msg.flags & REMOTE_REPLY)
				error = rem_ptr->msg.cmd;
			break;
#endif /* RWL_SERIAL */
		case REMOTE_DONGLE:
			if (*len > SERVER_RESPONSE_MAX_BUF_LEN)
				*len = SERVER_RESPONSE_MAX_BUF_LEN;

			/* Actual buffer to be sent should be max 256 bytes as
			 *UART input buffer
			 * is 512 bytes
			 */
			tx_len = MIN(*len, 512);
			error = rwl_information_dongle(wl, cmd, buf, len, tx_len, rem_ioctl_select);
			break;

		case REMOTE_WIFI:
#ifdef RWL_WIFI
			if (*len > SERVER_RESPONSE_MAX_BUF_LEN)
				*len = SERVER_RESPONSE_MAX_BUF_LEN;

			/* Actual buffer to be sent should be max 960 bytes
			 * as wifi max frame size if 960
			 * and actual data for any command will not exceed 960 bytes
			*/
			tx_len = MIN(*len, RWL_WIFI_FRAG_DATA_SIZE);

			if ((cbuf = (char*) malloc(tx_len)) == NULL) {
				DPRINT_ERR(ERR, "Malloc failed for set_info_fe character buf\n");
				return FAIL;
			}
			if ((list = (dot11_action_wifi_vendor_specific_t *)
					malloc(RWL_WIFI_ACTION_FRAME_SIZE)) == NULL) {
				free(cbuf);
				return FAIL;
			}

			if ((rem_ptr = (rem_ioctl_t *)malloc(sizeof(rem_ioctl_t))) == NULL) {
				free(list);
				free(cbuf);
				return FAIL;
			}

			memcpy(cbuf, (char*)buf, tx_len);

			for (retry = 0; retry < RWL_WIFI_RETRY; retry++) {

				rwl_wifi_purge_actionframes(wl);
				/* copy back the buffer to input buffer */
				memcpy((char*)buf, (char*)cbuf, tx_len);

				if (retry > 3)
					DPRINT_INFO(OUTPUT, "retry %d cmd %d\n", retry, cmd);

				if ((error = remote_CDC_wifi_tx(wl, cmd, buf, *len,
				                                tx_len, rem_ioctl_select) < 0)) {
					DPRINT_ERR(ERR, "ir_setinformation_fe: Send"
					"command failed\n");
					rwl_sleep(RWL_WIFI_RETRY_DELAY);
					continue;
				}

				if ((char*)buf != NULL) {
					/* In case cmd is findserver, response is not
					 * required from the server
					 */
					if (!strcmp((char*)buf, RWL_WIFI_FIND_SER_CMD)) {
						break;
					}
				}

				/* Read the CDC header and data of for the sent cmd
				 * resposne
				 */
				if ((error = remote_CDC_DATA_wifi_rx((void*)wl, list) < 0)) {
					DPRINT_ERR(ERR, "ir_setinformation_fe: failed to read"
						"the response\n");
					rwl_sleep(RWL_WIFI_RETRY_DELAY);
					continue;
				}

				memcpy((char*)rem_ptr,
				(char*)&list->data[RWL_WIFI_CDC_HEADER_OFFSET],
				REMOTE_SIZE);
				rwl_swap_header(rem_ptr, NETWORK_TO_HOST);

				memcpy((char*)buf, (char*)&list->data[REMOTE_SIZE],
				rem_ptr->msg.len);

				if (rem_ptr->msg.flags & REMOTE_REPLY) {
					error = rem_ptr->msg.cmd;
					break;
				} else {
					rwl_sleep(RWL_WIFI_RETRY_DELAY);
				}
			}
				free(rem_ptr);
				free(list);
			if (cbuf != NULL)
				free(cbuf);
			break;
#endif /* RWL_WIFI */
		default:
			DPRINT_ERR(ERR, "rwl_setinformation_fe: Unknown remote_type:%d\n",
			remote_type);
		break;
	}

	return error;
}

#ifdef RWL_WIFI
int
rwl_var_getbuf(void *wl, const char *iovar, void *param, int param_len, void **bufptr)
{
	int len, error;

	memset((char*)g_rwl_aligned_buf, 0, WLC_IOCTL_MAXLEN);
	strcpy(g_rwl_aligned_buf, iovar);

	/* include the null */
	len = strlen(iovar) + 1;

	if (param_len)
		memcpy(&g_rwl_aligned_buf[len], param, param_len);

	*bufptr = g_rwl_aligned_buf;
	/*   When user wants to execute local CMD being in remote wifi mode, 
	*	rwl_wifi_swap_remote_type fucntion is used to change the remote types.
	*/
	rwl_wifi_swap_remote_type(remote_type);

	error = wl_get(wl, WLC_GET_VAR, &g_rwl_aligned_buf[0], WLC_IOCTL_MAXLEN);
	/* revert back to the old remote type */
	rwl_wifi_swap_remote_type(remote_type);
	return error;
}

int
rwl_var_setbuf(void *wl, const char *iovar, void *param, int param_len)
{
	int len, error;

	memset(g_rwl_aligned_buf, 0, WLC_IOCTL_MAXLEN);
	strcpy(g_rwl_aligned_buf, iovar);

	/* include the null */
	len = strlen(iovar) + 1;

	if (param_len)
		memcpy(&g_rwl_aligned_buf[len], param, param_len);

	len += param_len;
	/*   When user wants to execute local CMD being in remote wifi mode, 
	*	rwl_wifi_swap_remote_type fucntion is used to change the remote types.
	*/
	rwl_wifi_swap_remote_type(remote_type);
	error = wl_set(wl, WLC_SET_VAR, &g_rwl_aligned_buf[0], len);
	/* revert back to the old type */
	rwl_wifi_swap_remote_type(remote_type);
	return error;
}

/* This function will send the buffer to the dongle driver */
int
rwl_var_send_vs_actionframe(void* wl, const char* iovar, void* param, int param_len)
{
	int len, error;

	memset(g_rwl_aligned_buf, 0, WLC_IOCTL_MAXLEN);
	strcpy(g_rwl_aligned_buf, iovar);

	/* include the null */
	len = strlen(iovar) + 1;

	if (param_len)
		memcpy((void*)&g_rwl_aligned_buf[len+ OFFSETOF(wl_action_frame_t, data)],
		param,
		param_len);

	/* Set the PacketID (not used by remote WL */
	memset((void*)&g_rwl_aligned_buf[len + OFFSETOF(wl_action_frame_t, packetId)], 0, 4);

	/* Set the dest addr */
	memcpy((void*)&g_rwl_aligned_buf[len + OFFSETOF(wl_action_frame_t, da)],
	(void*)g_rwl_buf_mac,
	ETHER_ADDR_LEN);

	/* set the length */
	memcpy((void*)&g_rwl_aligned_buf[len + OFFSETOF(wl_action_frame_t, len)],
	(void*) &param_len,
	2);

	len  += param_len + ETHER_ADDR_LEN + 2 + 4;

	/*   When user wants to execute local CMD being in remote wifi mode, 
	*	rwl_wifi_swap_remote_type fucntion is used to change the remote types.
	*/
	rwl_wifi_swap_remote_type(remote_type);

	error = wl_set(wl, WLC_SET_VAR, &g_rwl_aligned_buf[0], len);
	/* revert back to the old type */
	rwl_wifi_swap_remote_type(remote_type);
	return error;
}
#endif /* RWL_WIFI */
/* 
 * Function for printing the usage if user type invalid command line
 * options(e.g wl --serial or --dongle or --socket or --wifi)
 */
void
rwl_usage(int remote_type)
{
	switch (remote_type) {
	case REMOTE_SERIAL:
			fprintf(stderr, " Usage: wl/dhd [--linuxdut/linux] [--debug]");
			fprintf(stderr, " [--serial port no]");
			fprintf(stderr, "[--ReadTimeout n] [--interactive] [--clientbatch] \n");
			fprintf(stderr, "\t--linuxdut/linux removes the WLC_OID_BASE");
			fprintf(stderr, "when sending the IOCTL command \n");
			fprintf(stderr, "\t--debug prints out tx packets sending down ");
			fprintf(stderr, " the serial line, and other debug info \n");
			fprintf(stderr, "\t--serial enables the remoteWL serial port number\n");
			fprintf(stderr, "\t--interactive enables using WL in interactive mode\n");
			fprintf(stderr, "\t--clientbatch enables command batchinng on the client,");
			fprintf(stderr, " the default is batching on driver\n");
		break;
	case REMOTE_DONGLE:
			fprintf(stderr, " Usage: wl/dhd --dongle <Device Name>  <command>\n");
			fprintf(stderr, "\t<Device Name> COM1/COM2 (WinXP) or /dev/ttyS0"
			" or /dev/ttyS1 (Linux)\n");
			fprintf(stderr, "\t<command> - wl, shell or dhd command\n");
			fprintf(stderr, "\tDepending on the client you are using(wl or dhd)\n");
			fprintf(stderr, "\t\t shell command usage: sh <command>\n");
		break;
	case REMOTE_SOCKET:
			fprintf(stderr, " Usage: wl/dhd --socket <IP ADDRESS> <PORT>\n");
			fprintf(stderr, "\t<IPADDRESS> IP address of server machine\n");
			fprintf(stderr, "\t<PORT> Port no of server\n");
			fprintf(stderr, "\t<command> - wl, shell or dhd command\n");
			fprintf(stderr, "\tDepending on the client you are using(wl or dhd)\n");
			fprintf(stderr, "\t\t shell command usage: sh <command>\n");
		break;
	case REMOTE_WIFI:
			fprintf(stderr, " Usage: wl/dhd --wifi <MAC Address> <command>\n");
			fprintf(stderr, "\t<MAC Address> MAC Address\n");
			fprintf(stderr, "\t<command> - wl, shell or dhd command\n");
			fprintf(stderr, "\tDepending on the client you are using(wl or dhd)\n");
			fprintf(stderr, "\t\t shell command usage: sh <command>\n");
		break;
	default:
		break;
	}
}
#ifdef RWL_SOCKET
static int
rwl_socket_shellresp(void *wl, rem_ioctl_t *rem_ptr, uchar *input_buf)
{
	uchar* resp_buf = NULL;
	int pid, msg_len, error;
	g_sig_ctrlc = 1;
	g_child_pid = pid = rwl_shell_createproc(wl);
	if (pid == 0) {
		while (g_sig_ctrlc);
		remote_CDC_tx(wl, 0, input_buf, 0, 0, CTRLC_FLAG, 0);
		exit(0);
	}

	do {
		if ((rem_ptr = remote_CDC_rx_hdr(wl, 0)) == NULL) {
		DPRINT_ERR(ERR, "rwl_socket_shellresp: Receiving CDC"
			"header failed\n");
		rwl_close_pipe(remote_type, wl);
		return FAIL;
		}
		msg_len = rem_ptr->msg.len;
		if ((resp_buf = malloc(rem_ptr->msg.len + 1)) == NULL) {
			DPRINT_ERR(ERR, "rwl_socket_shellresp: Mem alloc fails\n");
			rwl_close_pipe(remote_type, wl);
			return FAIL;
		}
		if (msg_len > 0) {
			if (remote_CDC_rx(wl, rem_ptr, resp_buf,
				rem_ptr->msg.len, 0) == FAIL) {
				DPRINT_ERR(ERR, "rwl_socket_shellresp: No results!\n");
				rwl_close_pipe(remote_type, wl);
				free(resp_buf);
				return FAIL;
			}
		}
		/* print the shell result */
		resp_buf[rem_ptr->msg.len] = '\0';
		/* The return value of the shell command 
		 * will be stored in rem_ptr->msg.cmd
		 * Return that value to the client process
		 */
		if (rem_ptr->msg.flags & REMOTE_REPLY)
			error = rem_ptr->msg.cmd;
		write(1, resp_buf, msg_len);
	} while (msg_len);
	rwl_shell_killproc(pid);
	return error;
}
#endif /* RWL_SOCKET */
/* For wifi shell responses read data until server stops sending */
#ifdef RWL_WIFI
static int
rwl_wifi_shellresp(void *wl, rem_ioctl_t *rem_ptr, uchar *input_buf)
{
	int pid, msg_len, error;
	g_sig_ctrlc = 1;
	g_child_pid = pid = rwl_shell_createproc(wl);
	if (pid == 0)
	{
		while (g_sig_ctrlc);
		remote_CDC_tx(wl, 0, input_buf, 0, 0, CTRLC_FLAG, 0);
		exit(0);
	}

	do {
		if ((error = remote_CDC_DATA_wifi_rx_frag(wl, rem_ptr, 0,
		NULL, RWL_WIFI_SHELL_CMD)) < 0) {
			DPRINT_DBG(OUTPUT, "rwl_shell_information_fe(wifi):"
			"error in reading shell response\n");
			continue;
		}
		msg_len = rem_ptr->msg.len;
		error = rem_ptr->msg.cmd;
	} while (msg_len);

	rwl_shell_killproc(pid);
	return error;
}
#endif /* RWL_WIFI */

/* For dongle or system serial shell responses read data until server stops sending */
static int
rwl_dongle_shellresp(void *wl, rem_ioctl_t *rem_ptr, uchar *input_buf, int cmd)
{
	int pid, msg_len, error;
	uchar *resp_buf;

	g_sig_ctrlc = 1;
	g_child_pid = pid = rwl_shell_createproc(wl);
	if (pid == 0) {
		while (g_sig_ctrlc);
		remote_CDC_tx(wl, cmd, input_buf, 0, 0, CTRLC_FLAG, 0);
		exit(0);
	}

	do {
		if ((rem_ptr = remote_CDC_rx_hdr(wl, 0)) == NULL) {
			DPRINT_ERR(ERR, "shell_info_fe: Receiving CDC header failed\n");
			return SERIAL_PORT_ERR;
		}
		/* In case of shell or ASD commands the response size is not known in advance
		* Hence based on response from the server memory is allocated
		*/
		msg_len = rem_ptr->msg.len;
		if ((resp_buf = malloc(rem_ptr->msg.len + 1)) == NULL) {
			DPRINT_ERR(ERR, "Mem alloc fails for shell response buffer\n");
			return FAIL;
		}
		/* Response comes in one shot not in fragments */
		if (remote_CDC_rx(wl, rem_ptr, resp_buf,
			rem_ptr->msg.len, 0) == FAIL) {
			DPRINT_ERR(ERR, "shell_info_fe: No results!\n");
			free(resp_buf);
			return FAIL;
		}
		/* print the shell result */
		resp_buf[rem_ptr->msg.len] = '\0';
		/* The return value of the shell command will be stored in rem_ptr->msg.cmd
		 * Return that value to the client process
		 */
		if (rem_ptr->msg.flags & REMOTE_REPLY)
			error = rem_ptr->msg.cmd;
		write(1, resp_buf, msg_len);
	} while (msg_len);
	rwl_shell_killproc(pid);
	return error;
}
