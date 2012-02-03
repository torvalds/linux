/*
 * OS independent remote wl declarations
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlu_remote.h,v 1.18.2.2 2010/12/22 18:59:03 Exp $
 */
#ifndef _wlu_remote_h
#define _wlu_remote_h
#include <wlioctl.h>
/* Remote wl declararions */
#define NO_REMOTE       0
#define REMOTE_SERIAL 	1
#define REMOTE_SOCKET 	2
#define REMOTE_WIFI     3
#define REMOTE_DONGLE   4
#define SHELL_CMD       -1 /* Invalid cmd id for shell */
#define ASD_CMD       	-2 /* Cmd id for ASD command */
#define SERVER_RESPONSE_MAX_BUF_LEN 8192
#define SHELL_RESP_SIZE 1024
#define CTRLC_PACKET 0xDEADBEAF
#define CTRLC_FLAG -4
#define VISTA_CMD	-3 /* cmd id for remote vista */

/* For cross OS support */
#define LINUX_OS  	1
#define WIN32_OS  	2
#define MAC_OSX		3
#define BACKLOG 	4
#define WINVISTA_OS	5
#define INDONGLE	6

/* Used in cdc_ioctl_t.flags field */
#define REMOTE_SET_IOCTL			1
#define REMOTE_GET_IOCTL			2
#define REMOTE_REPLY				4
#define REMOTE_SHELL_CMD			8
#define REMOTE_FINDSERVER_IOCTL		16 /* Find remote server */
#define REMOTE_ASD_CMD				32 /* ASD integration */
#define RDHD_SET_IOCTL			64
#define RDHD_GET_IOCTL			128
#define REMOTE_VISTA_CMD			256 /* for remote vista specific commands */
#define RWL_WIFI_DEFAULT_TYPE 		0x00
#define RWL_WIFI_DEFAULT_SUBTYPE 	0x00
#define RWL_ACTION_FRAME_DATA_SIZE 	1024	/* fixed size for the wifi frame data */
#define RWL_WIFI_CDC_HEADER_OFFSET 	0
#define RWL_WIFI_FRAG_DATA_SIZE 	960 /* max size of the frag data */
#define RWL_DEFAULT_WIFI_FRAG_COUNT 127 /* maximum fragment count */
#define RWL_WIFI_RETRY				5	/* CMD retry count for wifi */
#define RWL_WIFI_RX_RETRY			20  /* WIFI response rerty count */
#define RWL_WIFI_SEND				5   /* WIFI frame sent count */
#define RWL_WIFI_RETRY_DELAY   		1000 /* wifi specific retry delay  */
#define RWL_WIFI_SEND_DELAY			100  /* delay between two frames */
#define RWL_WIFI_RX_DELAY			250  /*  wait between send and receive */
#define RWL_WIFI_RX_SHELL_DELAY		1000 /* delay added for shell cmd response read */
#define RWL_CHANNEL_RX_SCAN_DELAY	10  /* Delay between findserver rx calls */
#define RWL_CHANNEL_SCAN_WAIT		250 /* Sleep time in between the channel scans */
#define RWL_WIFI_BUF_LEN			64
#define RWL_WIFI_SHELL_CMD			1
#define RWL_WIFI_WL_CMD            	0
#define RWL_WIFI_FIND_SER_CMD 		"findserver"
#define RWL_WIFI_ACTION_CMD   		"wifiaction"
#define RWL_WIFI_GET_ACTION_CMD         "rwlwifivsaction"
#define RWL_DONGLE_SET_CMD			"dongleset"
#define DATA_FRAME_LEN				960
/* wl & shell cmd work fine for 960 (512+256+128+64) */


/* 
 * Information about the action frame data fields in the
 * dot11_action_wifi_vendor_specific
 * cdc struct (1 to 16. This does not include status flag. Since this
 * is not directly visible to the driver code we cant use sizeof struct
 * cdc_ioctl_t. Hence Ref MAC address offset starts from byte 17.
 * REF MAC ADDR (6 bytes (MAC Address len) from byte 17 to 22)
 * DUT MAC ADDR (6 bytes after the REF MAC Address byte 23 to 28)
 * unused (byte 29 to 49)
 * REF/Client Channel offset (50)
 * DUT/Server channel offset (51)
 * ---------------------------------------------------------------------------------------
 * cdc struct|REF MAC ADDR|DUT_MAC_ADDR|un used|REF Channel|DUT channel|Action frame Data|
 * 1---------17-----------23-------------------50----------51----------52----------------1040
 * REF MAC addr after CDC struct without status flag (status flag not used by wifi)
 */
#define RWL_ACTION_WIFI_CATEGORY	127  /* Vendor Specific category value for wifi */
#define RWL_REF_MAC_ADDRESS_OFFSET	17
#define RWL_DUT_MAC_ADDRESS_OFFSET	23
#define RWL_WIFI_CLIENT_CHANNEL_OFFSET	50
#define RWL_WIFI_SERVER_CHANNEL_OFFSET	51

#define SUCCESS 					1
#define FAIL   						-1
#define NO_PACKET        			-2
#define SERIAL_PORT_ERR  			-3

#define DEFAULT_SERVER_PORT   		8000

#define WL_MAX_BUF_LEN 				(127 * 1024)
#define MAX_VISTA_ARGC				10
#define HOST_TO_NETWORK				TRUE
#define NETWORK_TO_HOST				FALSE

#ifndef IFNAMSIZ
#define IFNAMSIZ				16
#endif

/* We don't want the server to allocate bigger buffers for some of the commands
 * like scanresults. Server can still allocate 8K memory and send the response
 * in fragments. This is used in case of Get commands only.
 */
#define SERVER_RESPONSE_MAX_BUF_LEN 8192
/* Used to set the cmds for wifi specific init */
typedef struct remote_wifi_cmds {
	uint32	cmd;		/* common ioctl definition */
	char	*data;		/* max size of the data length */
	int		value;
} remote_wifi_cmds_t;

/* Added for debug utility support */
#define ERR		stderr
#define OUTPUT	stdout

#define DEBUG_DEFAULT	0x0001
#define DEBUG_ERR		0x0001
#define DEBUG_INFO		0x0002
#define DEBUG_DBG		0x0004

#define DPRINT_ERR	if (defined_debug & DEBUG_ERR) \
			    fprintf
#define DPRINT_INFO	if (defined_debug & DEBUG_INFO) \
			    fprintf
#define DPRINT_DBG	if (defined_debug & DEBUG_DBG) \
			    fprintf
extern unsigned short defined_debug;
#ifdef WIN32
#define IPV4_ADDR_LEN 4
/* IPV4 packet formats */
struct ipv4_addr {
	uint8	addr[IPV4_ADDR_LEN];
} PACKED;
/* Function defined in wlu.c for client and wlu_server_ce.c for server */
extern int wl_atoip(const char *a, struct ipv4_addr *n);
#endif
/* Function defined to do host to network and network to host conversions */
void rwl_swap_header(rem_ioctl_t *rem_ptr, bool host_to_network);

/* Macros to access remote type */
extern int remote_type;
#define rwl_get_remote_type()		(remote_type)
#define rwl_set_remote_type(type)	(remote_type = type)

/* Macros to access server IP and port globals */
extern char *g_rwl_servIP;
#define rwl_get_server_ip()			(g_rwl_servIP)
#define rwl_set_server_ip(ip) 		(g_rwl_servIP = ip)
extern unsigned short g_rwl_servport;
#define rwl_get_server_port()		(g_rwl_servport)
#define rwl_set_server_port(port) 	(g_rwl_servport = port)

extern int rwl_connectsocket(int SocketDes, struct sockaddr* SerAddr, int SizeOfAddr);
extern int rwl_send_to_streamsocket(int SocketDes, const char* SendBuff, int data_size, int Flag);
extern int rwl_receive_from_streamsocket(int SocketDes, char* RecvBuff, int data_size, int Flag);
extern int rwl_read_serial_port(void* hndle, char* read_buf, uint data_size, uint *numread);
extern int rwl_write_serial_port(void* hndle, char* write_buf, unsigned long size,
unsigned long *numwritten);
extern void rwl_sleep(int delay);
extern void* rwl_open_transport(int remote_type, char *port, int ReadTotalTimeout, int debug);
extern int rwl_close_transport(int remote_type, void * handle);
extern int rwl_poll_serial_buffer(void *hndle);
extern int rwl_serial_handle_timeout(void);
extern void rwl_sync_delay(uint noframes);
extern int rwl_init_server_socket_setup(int argc, char** argv, uint remote_type);

extern int rwl_GetifAddr(char *ifname, struct sockaddr_in* sa);
extern  int rwl_acceptconnection(int SocketDes, struct sockaddr *ClientAddr, int *SizeOfAddr);
extern int set_interface(void *wl, char *intf_name);
/* Linux specific function in wlu_pipe_linux.c */
extern int rwl_get_if_addr(char *ifname, struct sockaddr_in *sa);

/* Win32 specific function wlu_pipe_win32.c */
extern int rwl_init_ws2_32dll(void);
extern int rwl_terminate_ws2_32dll(void);

/* Function definitions for wlu_client_shared.c and wlu_server_shared.c */
extern int rwl_var_getbuf(void *wl, const char *iovar, void *param, int param_len, void **bufptr);
extern int rwl_var_setbuf(void *wl, const char *iovar, void *param, int param_len);
extern int rwl_var_send_vs_actionframe(void* wl, const char* iovar, void* param, int param_len);

/* Function definitions for wlu_ndis.c/wlu_linux.c  and wlu_server_ce.c/wlu_server_linux.c  */
extern int wl_get(void *wl, int cmd, void *buf, int len);
extern int wl_set(void *wl, int cmd, void *buf, int len);
#ifdef RWLASD
typedef unsigned char BYTE;
extern void wfa_dut_init(BYTE **tBuf, BYTE **rBuf, BYTE **paBuf, BYTE **cBuf, struct timeval **);
extern int remote_asd_exec(unsigned char* command, int* cmd_len);
void wfa_dut_deinit(void);
#endif
extern int get_ctrlc_header(void *wl);
extern int remote_tx_response(void *wl, void *buf_ptr, int cmd);
extern rem_ioctl_t *g_rem_ptr;
extern int g_rwl_hndle;
/* Variable to indiate if the server child has completed execution */
extern volatile sig_atomic_t g_sig_chld;

#ifndef WIN32
#ifdef TARGETENV_android
/*
 * Separate paths are defined for Android and Linux machines
 * / filesystem on Android is read only memory
 */
#define SH_PATH "/data/busybox/sh"
#define SHELL_RESP_PATH  "/data/local/RWL/" /* Default path for storing files for shell response */
#define	TEMPLATE         "/data/local/RWL/SyncXXXXXX"
#else
#define SHELL_RESP_PATH  "/tmp/RWL/" /* Default path for storing files for shell response */
#define	TEMPLATE         "/tmp/RWL/SyncXXXXXX"
#define SH_PATH "/bin/sh"
#endif
#endif /* !WIN32 */
#endif /* _wlu_remote_h */
