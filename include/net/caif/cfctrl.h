/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CFCTRL_H_
#define CFCTRL_H_
#include <net/caif/caif_layer.h>
#include <net/caif/cfsrvl.h>

/* CAIF Control packet commands */
enum cfctrl_cmd {
	CFCTRL_CMD_LINK_SETUP = 0,
	CFCTRL_CMD_LINK_DESTROY = 1,
	CFCTRL_CMD_LINK_ERR = 2,
	CFCTRL_CMD_ENUM = 3,
	CFCTRL_CMD_SLEEP = 4,
	CFCTRL_CMD_WAKE = 5,
	CFCTRL_CMD_LINK_RECONF = 6,
	CFCTRL_CMD_START_REASON = 7,
	CFCTRL_CMD_RADIO_SET = 8,
	CFCTRL_CMD_MODEM_SET = 9,
	CFCTRL_CMD_MASK = 0xf
};

/* Channel types */
enum cfctrl_srv {
	CFCTRL_SRV_DECM = 0,
	CFCTRL_SRV_VEI = 1,
	CFCTRL_SRV_VIDEO = 2,
	CFCTRL_SRV_DBG = 3,
	CFCTRL_SRV_DATAGRAM = 4,
	CFCTRL_SRV_RFM = 5,
	CFCTRL_SRV_UTIL = 6,
	CFCTRL_SRV_MASK = 0xf
};

#define CFCTRL_RSP_BIT 0x20
#define CFCTRL_ERR_BIT 0x10

struct cfctrl_rsp {
	void (*linksetup_rsp)(struct cflayer *layer, u8 linkid,
			      enum cfctrl_srv serv, u8 phyid,
			      struct cflayer *adapt_layer);
	void (*linkdestroy_rsp)(struct cflayer *layer, u8 linkid);
	void (*linkerror_ind)(void);
	void (*enum_rsp)(void);
	void (*sleep_rsp)(void);
	void (*wake_rsp)(void);
	void (*restart_rsp)(void);
	void (*radioset_rsp)(void);
	void (*reject_rsp)(struct cflayer *layer, u8 linkid,
				struct cflayer *client_layer);
};

/* Link Setup Parameters for CAIF-Links. */
struct cfctrl_link_param {
	enum cfctrl_srv linktype;/* (T3,T0) Type of Channel */
	u8 priority;		  /* (P4,P0) Priority of the channel */
	u8 phyid;		  /* (U2-U0) Physical interface to connect */
	u8 endpoint;		  /* (E1,E0) Endpoint for data channels */
	u8 chtype;		  /* (H1,H0) Channel-Type, applies to
				   *            VEI, DEBUG */
	union {
		struct {
			u8 connid;	/*  (D7,D0) Video LinkId */
		} video;

		struct {
			u32 connid;	/* (N31,Ngit0) Connection ID used
					 *  for Datagram */
		} datagram;

		struct {
			u32 connid;	/* Connection ID used for RFM */
			char volume[20];	/* Volume to mount for RFM */
		} rfm;		/* Configuration for RFM */

		struct {
			u16 fifosize_kb;	/* Psock FIFO size in KB */
			u16 fifosize_bufs;	/* Psock # signal buffers */
			char name[16];	/* Name of the PSOCK service */
			u8 params[255];	/* Link setup Parameters> */
			u16 paramlen;	/* Length of Link Setup
						 *   Parameters */
		} utility;	/* Configuration for Utility Links (Psock) */
	} u;
};

/* This structure is used internally in CFCTRL */
struct cfctrl_request_info {
	int sequence_no;
	enum cfctrl_cmd cmd;
	u8 channel_id;
	struct cfctrl_link_param param;
	struct cflayer *client_layer;
	struct list_head list;
};

struct cfctrl {
	struct cfsrvl serv;
	struct cfctrl_rsp res;
	atomic_t req_seq_no;
	atomic_t rsp_seq_no;
	struct list_head list;
	/* Protects from simultaneous access to first_req list */
	spinlock_t info_list_lock;
#ifndef CAIF_NO_LOOP
	u8 loop_linkid;
	int loop_linkused[256];
	/* Protects simultaneous access to loop_linkid and loop_linkused */
	spinlock_t loop_linkid_lock;
#endif

};

void cfctrl_enum_req(struct cflayer *cfctrl, u8 physlinkid);
int cfctrl_linkup_request(struct cflayer *cfctrl,
			   struct cfctrl_link_param *param,
			   struct cflayer *user_layer);
int  cfctrl_linkdown_req(struct cflayer *cfctrl, u8 linkid,
			 struct cflayer *client);
void cfctrl_sleep_req(struct cflayer *cfctrl);
void cfctrl_wake_req(struct cflayer *cfctrl);
void cfctrl_getstartreason_req(struct cflayer *cfctrl);
struct cflayer *cfctrl_create(void);
void cfctrl_set_dnlayer(struct cflayer *this, struct cflayer *dn);
void cfctrl_set_uplayer(struct cflayer *this, struct cflayer *up);
struct cfctrl_rsp *cfctrl_get_respfuncs(struct cflayer *layer);
bool cfctrl_req_eq(struct cfctrl_request_info *r1,
		   struct cfctrl_request_info *r2);
void cfctrl_insert_req(struct cfctrl *ctrl,
			      struct cfctrl_request_info *req);
struct cfctrl_request_info *cfctrl_remove_req(struct cfctrl *ctrl,
					      struct cfctrl_request_info *req);
void cfctrl_cancel_req(struct cflayer *layr, struct cflayer *adap_layer);

#endif				/* CFCTRL_H_ */
