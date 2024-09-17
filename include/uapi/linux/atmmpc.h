/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ATMMPC_H_
#define _ATMMPC_H_

#include <linux/atmapi.h>
#include <linux/atmioc.h>
#include <linux/atm.h>
#include <linux/types.h>

#define ATMMPC_CTRL _IO('a', ATMIOC_MPOA)
#define ATMMPC_DATA _IO('a', ATMIOC_MPOA+1)

#define MPC_SOCKET_INGRESS 1
#define MPC_SOCKET_EGRESS  2

struct atmmpc_ioc {
        int dev_num;
        __be32 ipaddr;              /* the IP address of the shortcut    */
        int type;                     /* ingress or egress                 */
};

typedef struct in_ctrl_info {
        __u8   Last_NHRP_CIE_code;
        __u8   Last_Q2931_cause_value;
        __u8   eg_MPC_ATM_addr[ATM_ESA_LEN];
        __be32  tag;
        __be32  in_dst_ip;      /* IP address this ingress MPC sends packets to */
        __u16  holding_time;
        __u32  request_id;
} in_ctrl_info;

typedef struct eg_ctrl_info {
        __u8   DLL_header[256];
        __u8   DH_length;
        __be32  cache_id;
        __be32  tag;
        __be32  mps_ip;
        __be32  eg_dst_ip;      /* IP address to which ingress MPC sends packets */
        __u8   in_MPC_data_ATM_addr[ATM_ESA_LEN];
        __u16  holding_time;
} eg_ctrl_info;

struct mpc_parameters {
        __u16 mpc_p1;   /* Shortcut-Setup Frame Count    */
        __u16 mpc_p2;   /* Shortcut-Setup Frame Time     */
        __u8 mpc_p3[8]; /* Flow-detection Protocols      */
        __u16 mpc_p4;   /* MPC Initial Retry Time        */
        __u16 mpc_p5;   /* MPC Retry Time Maximum        */
        __u16 mpc_p6;   /* Hold Down Time                */
} ;

struct k_message {
        __u16 type;
        __be32 ip_mask;
        __u8  MPS_ctrl[ATM_ESA_LEN];
        union {
                in_ctrl_info in_info;
                eg_ctrl_info eg_info;
                struct mpc_parameters params;
        } content;
        struct atm_qos qos;       
} __ATM_API_ALIGN;

struct llc_snap_hdr {
	/* RFC 1483 LLC/SNAP encapsulation for routed IP PDUs */
        __u8  dsap;    /* Destination Service Access Point (0xAA)     */
        __u8  ssap;    /* Source Service Access Point      (0xAA)     */
        __u8  ui;      /* Unnumbered Information           (0x03)     */
        __u8  org[3];  /* Organizational identification    (0x000000) */
        __u8  type[2]; /* Ether type (for IP)              (0x0800)   */
};

/* TLVs this MPC recognizes */
#define TLV_MPOA_DEVICE_TYPE         0x00a03e2a  

/* MPOA device types in MPOA Device Type TLV */
#define NON_MPOA    0
#define MPS         1
#define MPC         2
#define MPS_AND_MPC 3


/* MPC parameter defaults */

#define MPC_P1 10  /* Shortcut-Setup Frame Count  */ 
#define MPC_P2 1   /* Shortcut-Setup Frame Time   */
#define MPC_P3 0   /* Flow-detection Protocols    */
#define MPC_P4 5   /* MPC Initial Retry Time      */
#define MPC_P5 40  /* MPC Retry Time Maximum      */
#define MPC_P6 160 /* Hold Down Time              */
#define HOLDING_TIME_DEFAULT 1200 /* same as MPS-p7 */

/* MPC constants */

#define MPC_C1 2   /* Retry Time Multiplier       */
#define MPC_C2 60  /* Initial Keep-Alive Lifetime */

/* Message types - to MPOA daemon */

#define SND_MPOA_RES_RQST    201
#define SET_MPS_CTRL_ADDR    202
#define SND_MPOA_RES_RTRY    203 /* Different type in a retry due to req id         */
#define STOP_KEEP_ALIVE_SM   204
#define EGRESS_ENTRY_REMOVED 205
#define SND_EGRESS_PURGE     206
#define DIE                  207 /* tell the daemon to exit()                       */
#define DATA_PLANE_PURGE     208 /* Data plane purge because of egress cache hit miss or dead MPS */
#define OPEN_INGRESS_SVC     209

/* Message types - from MPOA daemon */

#define MPOA_TRIGGER_RCVD     101
#define MPOA_RES_REPLY_RCVD   102
#define INGRESS_PURGE_RCVD    103
#define EGRESS_PURGE_RCVD     104
#define MPS_DEATH             105
#define CACHE_IMPOS_RCVD      106
#define SET_MPC_CTRL_ADDR     107 /* Our MPC's control ATM address   */
#define SET_MPS_MAC_ADDR      108
#define CLEAN_UP_AND_EXIT     109
#define SET_MPC_PARAMS        110 /* MPC configuration parameters    */

/* Message types - bidirectional */       

#define RELOAD                301 /* kill -HUP the daemon for reload */

#endif /* _ATMMPC_H_ */
