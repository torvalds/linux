/*********************************************************************
 *                
 * Filename:      irmod.h
 * Version:       0.3
 * Description:   IrDA module and utilities functions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Dec 15 13:58:52 1997
 * Modified at:   Fri Jan 28 13:15:24 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-2000 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2002 Jean Tourrilhes <jt@hpl.hp.com>
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charg.
 *     
 ********************************************************************/

#ifndef IRMOD_H
#define IRMOD_H

/* Misc status information */
typedef enum {
	STATUS_OK,
	STATUS_ABORTED,
	STATUS_NO_ACTIVITY,
	STATUS_NOISY,
	STATUS_REMOTE,
} LINK_STATUS;

typedef enum {
	LOCK_NO_CHANGE,
	LOCK_LOCKED,
	LOCK_UNLOCKED,
} LOCK_STATUS;

typedef enum { FLOW_STOP, FLOW_START } LOCAL_FLOW;

/*  
 *  IrLMP disconnect reasons. The order is very important, since they 
 *  correspond to disconnect reasons sent in IrLMP disconnect frames, so
 *  please do not touch :-)
 */
typedef enum {
	LM_USER_REQUEST = 1,  /* User request */
	LM_LAP_DISCONNECT,    /* Unexpected IrLAP disconnect */
	LM_CONNECT_FAILURE,   /* Failed to establish IrLAP connection */
	LM_LAP_RESET,         /* IrLAP reset */
	LM_INIT_DISCONNECT,   /* Link Management initiated disconnect */
	LM_LSAP_NOTCONN,      /* Data delivered on unconnected LSAP */
	LM_NON_RESP_CLIENT,   /* Non responsive LM-MUX client */
	LM_NO_AVAIL_CLIENT,   /* No available LM-MUX client */
	LM_CONN_HALF_OPEN,    /* Connection is half open */
	LM_BAD_SOURCE_ADDR,   /* Illegal source address (i.e 0x00) */
} LM_REASON;
#define LM_UNKNOWN 0xff       /* Unspecified disconnect reason */

/* A few forward declarations (to make compiler happy) */
struct qos_info;		/* in <net/irda/qos.h> */

/*
 *  Notify structure used between transport and link management layers
 */
typedef struct {
	int (*data_indication)(void *priv, void *sap, struct sk_buff *skb);
	int (*udata_indication)(void *priv, void *sap, struct sk_buff *skb);
	void (*connect_confirm)(void *instance, void *sap, 
				struct qos_info *qos, __u32 max_sdu_size,
				__u8 max_header_size, struct sk_buff *skb);
	void (*connect_indication)(void *instance, void *sap, 
				   struct qos_info *qos, __u32 max_sdu_size, 
				   __u8 max_header_size, struct sk_buff *skb);
	void (*disconnect_indication)(void *instance, void *sap, 
				      LM_REASON reason, struct sk_buff *);
	void (*flow_indication)(void *instance, void *sap, LOCAL_FLOW flow);
	void (*status_indication)(void *instance,
				  LINK_STATUS link, LOCK_STATUS lock);
	void *instance; /* Layer instance pointer */
	char name[16];  /* Name of layer */
} notify_t;

#define NOTIFY_MAX_NAME 16

/* Zero the notify structure */
void irda_notify_init(notify_t *notify);

/* Locking wrapper - Note the inverted logic on irda_lock().
 * Those function basically return false if the lock is already in the
 * position you want to set it. - Jean II */
#define irda_lock(lock)		(! test_and_set_bit(0, (void *) (lock)))
#define irda_unlock(lock)	(test_and_clear_bit(0, (void *) (lock)))

#endif /* IRMOD_H */









