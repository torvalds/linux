/*********************************************************************
 *
 * Filename:      iriap.c
 * Version:       0.8
 * Description:   Information Access Protocol (IAP)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Aug 21 00:02:07 1997
 * Modified at:   Sat Dec 25 16:42:42 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>,
 *     All Rights Reserved.
 *     Copyright (c) 2000-2003 Jean Tourrilhes <jt@hpl.hp.com>
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/seq_file.h>

#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/irias_object.h>
#include <net/irda/iriap_event.h>
#include <net/irda/iriap.h>

#ifdef CONFIG_IRDA_DEBUG
/* FIXME: This one should go in irlmp.c */
static const char *ias_charset_types[] = {
	"CS_ASCII",
	"CS_ISO_8859_1",
	"CS_ISO_8859_2",
	"CS_ISO_8859_3",
	"CS_ISO_8859_4",
	"CS_ISO_8859_5",
	"CS_ISO_8859_6",
	"CS_ISO_8859_7",
	"CS_ISO_8859_8",
	"CS_ISO_8859_9",
	"CS_UNICODE"
};
#endif	/* CONFIG_IRDA_DEBUG */

static hashbin_t *iriap = NULL;
static void *service_handle;

static void __iriap_close(struct iriap_cb *self);
static int iriap_register_lsap(struct iriap_cb *self, __u8 slsap_sel, int mode);
static void iriap_disconnect_indication(void *instance, void *sap,
					LM_REASON reason, struct sk_buff *skb);
static void iriap_connect_indication(void *instance, void *sap,
				     struct qos_info *qos, __u32 max_sdu_size,
				     __u8 max_header_size,
				     struct sk_buff *skb);
static void iriap_connect_confirm(void *instance, void *sap,
				  struct qos_info *qos,
				  __u32 max_sdu_size, __u8 max_header_size,
				  struct sk_buff *skb);
static int iriap_data_indication(void *instance, void *sap,
				 struct sk_buff *skb);

static void iriap_watchdog_timer_expired(void *data);

static inline void iriap_start_watchdog_timer(struct iriap_cb *self, 
					      int timeout) 
{
	irda_start_timer(&self->watchdog_timer, timeout, self, 
			 iriap_watchdog_timer_expired);
}

/*
 * Function iriap_init (void)
 *
 *    Initializes the IrIAP layer, called by the module initialization code
 *    in irmod.c
 */
int __init iriap_init(void)
{
	struct ias_object *obj;
	struct iriap_cb *server;
	__u8 oct_seq[6];
	__u16 hints;

	/* Allocate master array */
	iriap = hashbin_new(HB_LOCK);
	if (!iriap)
		return -ENOMEM;

	/* Object repository - defined in irias_object.c */
	irias_objects = hashbin_new(HB_LOCK);
	if (!irias_objects) {
		IRDA_WARNING("%s: Can't allocate irias_objects hashbin!\n",
			     __FUNCTION__);
		hashbin_delete(iriap, NULL);
		return -ENOMEM;
	}

	/*
	 *  Register some default services for IrLMP
	 */
	hints  = irlmp_service_to_hint(S_COMPUTER);
	service_handle = irlmp_register_service(hints);

	/* Register the Device object with LM-IAS */
	obj = irias_new_object("Device", IAS_DEVICE_ID);
	irias_add_string_attrib(obj, "DeviceName", "Linux", IAS_KERNEL_ATTR);

	oct_seq[0] = 0x01;  /* Version 1 */
	oct_seq[1] = 0x00;  /* IAS support bits */
	oct_seq[2] = 0x00;  /* LM-MUX support bits */
#ifdef CONFIG_IRDA_ULTRA
	oct_seq[2] |= 0x04; /* Connectionless Data support */
#endif
	irias_add_octseq_attrib(obj, "IrLMPSupport", oct_seq, 3,
				IAS_KERNEL_ATTR);
	irias_insert_object(obj);

	/*
	 *  Register server support with IrLMP so we can accept incoming
	 *  connections
	 */
	server = iriap_open(LSAP_IAS, IAS_SERVER, NULL, NULL);
	if (!server) {
		IRDA_DEBUG(0, "%s(), unable to open server\n", __FUNCTION__);
		return -1;
	}
	iriap_register_lsap(server, LSAP_IAS, IAS_SERVER);

	return 0;
}

/*
 * Function iriap_cleanup (void)
 *
 *    Initializes the IrIAP layer, called by the module cleanup code in
 *    irmod.c
 */
void __exit iriap_cleanup(void)
{
	irlmp_unregister_service(service_handle);

	hashbin_delete(iriap, (FREE_FUNC) __iriap_close);
	hashbin_delete(irias_objects, (FREE_FUNC) __irias_delete_object);
}

/*
 * Function iriap_open (void)
 *
 *    Opens an instance of the IrIAP layer, and registers with IrLMP
 */
struct iriap_cb *iriap_open(__u8 slsap_sel, int mode, void *priv,
			    CONFIRM_CALLBACK callback)
{
	struct iriap_cb *self;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	self = kmalloc(sizeof(struct iriap_cb), GFP_ATOMIC);
	if (!self) {
		IRDA_WARNING("%s: Unable to kmalloc!\n", __FUNCTION__);
		return NULL;
	}

	/*
	 *  Initialize instance
	 */
	memset(self, 0, sizeof(struct iriap_cb));

	self->magic = IAS_MAGIC;
	self->mode = mode;
	if (mode == IAS_CLIENT)
		iriap_register_lsap(self, slsap_sel, mode);

	self->confirm = callback;
	self->priv = priv;

	/* iriap_getvaluebyclass_request() will construct packets before
	 * we connect, so this must have a sane value... Jean II */
	self->max_header_size = LMP_MAX_HEADER;

	init_timer(&self->watchdog_timer);

	hashbin_insert(iriap, (irda_queue_t *) self, (long) self, NULL);

	/* Initialize state machines */
	iriap_next_client_state(self, S_DISCONNECT);
	iriap_next_call_state(self, S_MAKE_CALL);
	iriap_next_server_state(self, R_DISCONNECT);
	iriap_next_r_connect_state(self, R_WAITING);

	return self;
}
EXPORT_SYMBOL(iriap_open);

/*
 * Function __iriap_close (self)
 *
 *    Removes (deallocates) the IrIAP instance
 *
 */
static void __iriap_close(struct iriap_cb *self)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	del_timer(&self->watchdog_timer);

	if (self->request_skb)
		dev_kfree_skb(self->request_skb);

	self->magic = 0;

	kfree(self);
}

/*
 * Function iriap_close (void)
 *
 *    Closes IrIAP and deregisters with IrLMP
 */
void iriap_close(struct iriap_cb *self)
{
	struct iriap_cb *entry;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	if (self->lsap) {
		irlmp_close_lsap(self->lsap);
		self->lsap = NULL;
	}

	entry = (struct iriap_cb *) hashbin_remove(iriap, (long) self, NULL);
	IRDA_ASSERT(entry == self, return;);

	__iriap_close(self);
}
EXPORT_SYMBOL(iriap_close);

static int iriap_register_lsap(struct iriap_cb *self, __u8 slsap_sel, int mode)
{
	notify_t notify;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	irda_notify_init(&notify);
	notify.connect_confirm       = iriap_connect_confirm;
	notify.connect_indication    = iriap_connect_indication;
	notify.disconnect_indication = iriap_disconnect_indication;
	notify.data_indication       = iriap_data_indication;
	notify.instance = self;
	if (mode == IAS_CLIENT)
		strcpy(notify.name, "IrIAS cli");
	else
		strcpy(notify.name, "IrIAS srv");

	self->lsap = irlmp_open_lsap(slsap_sel, &notify, 0);
	if (self->lsap == NULL) {
		IRDA_ERROR("%s: Unable to allocated LSAP!\n", __FUNCTION__);
		return -1;
	}
	self->slsap_sel = self->lsap->slsap_sel;

	return 0;
}

/*
 * Function iriap_disconnect_indication (handle, reason)
 *
 *    Got disconnect, so clean up everything associated with this connection
 *
 */
static void iriap_disconnect_indication(void *instance, void *sap,
					LM_REASON reason,
					struct sk_buff *skb)
{
	struct iriap_cb *self;

	IRDA_DEBUG(4, "%s(), reason=%s\n", __FUNCTION__, irlmp_reasons[reason]);

	self = (struct iriap_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	IRDA_ASSERT(iriap != NULL, return;);

	del_timer(&self->watchdog_timer);

	/* Not needed */
	if (skb)
		dev_kfree_skb(skb);

	if (self->mode == IAS_CLIENT) {
		IRDA_DEBUG(4, "%s(), disconnect as client\n", __FUNCTION__);


		iriap_do_client_event(self, IAP_LM_DISCONNECT_INDICATION,
				      NULL);
		/*
		 * Inform service user that the request failed by sending
		 * it a NULL value. Warning, the client might close us, so
		 * remember no to use self anymore after calling confirm
		 */
		if (self->confirm)
			self->confirm(IAS_DISCONNECT, 0, NULL, self->priv);
	} else {
		IRDA_DEBUG(4, "%s(), disconnect as server\n", __FUNCTION__);
		iriap_do_server_event(self, IAP_LM_DISCONNECT_INDICATION,
				      NULL);
		iriap_close(self);
	}
}

/*
 * Function iriap_disconnect_request (handle)
 */
static void iriap_disconnect_request(struct iriap_cb *self)
{
	struct sk_buff *tx_skb;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	tx_skb = dev_alloc_skb(64);
	if (tx_skb == NULL) {
		IRDA_DEBUG(0, "%s(), Could not allocate an sk_buff of length %d\n", 
			__FUNCTION__, 64);
		return;
	}

	/*
	 *  Reserve space for MUX control and LAP header
	 */
	skb_reserve(tx_skb, LMP_MAX_HEADER);

	irlmp_disconnect_request(self->lsap, tx_skb);
}

/*
 * Function iriap_getvaluebyclass (addr, name, attr)
 *
 *    Retrieve all values from attribute in all objects with given class
 *    name
 */
int iriap_getvaluebyclass_request(struct iriap_cb *self,
				  __u32 saddr, __u32 daddr,
				  char *name, char *attr)
{
	struct sk_buff *tx_skb;
	int name_len, attr_len, skb_len;
	__u8 *frame;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return -1;);

	/* Client must supply the destination device address */
	if (!daddr)
		return -1;

	self->daddr = daddr;
	self->saddr = saddr;

	/*
	 *  Save operation, so we know what the later indication is about
	 */
	self->operation = GET_VALUE_BY_CLASS;

	/* Give ourselves 10 secs to finish this operation */
	iriap_start_watchdog_timer(self, 10*HZ);

	name_len = strlen(name);	/* Up to IAS_MAX_CLASSNAME = 60 */
	attr_len = strlen(attr);	/* Up to IAS_MAX_ATTRIBNAME = 60 */

	skb_len = self->max_header_size+2+name_len+1+attr_len+4;
	tx_skb = dev_alloc_skb(skb_len);
	if (!tx_skb)
		return -ENOMEM;

	/* Reserve space for MUX and LAP header */
	skb_reserve(tx_skb, self->max_header_size);
	skb_put(tx_skb, 3+name_len+attr_len);
	frame = tx_skb->data;

	/* Build frame */
	frame[0] = IAP_LST | GET_VALUE_BY_CLASS;
	frame[1] = name_len;                       /* Insert length of name */
	memcpy(frame+2, name, name_len);           /* Insert name */
	frame[2+name_len] = attr_len;              /* Insert length of attr */
	memcpy(frame+3+name_len, attr, attr_len);  /* Insert attr */

	iriap_do_client_event(self, IAP_CALL_REQUEST_GVBC, tx_skb);

	/* Drop reference count - see state_s_disconnect(). */
	dev_kfree_skb(tx_skb);

	return 0;
}
EXPORT_SYMBOL(iriap_getvaluebyclass_request);

/*
 * Function iriap_getvaluebyclass_confirm (self, skb)
 *
 *    Got result from GetValueByClass command. Parse it and return result
 *    to service user.
 *
 */
static void iriap_getvaluebyclass_confirm(struct iriap_cb *self,
					  struct sk_buff *skb)
{
	struct ias_value *value;
	int charset;
	__u32 value_len;
	__u32 tmp_cpu32;
	__u16 obj_id;
	__u16 len;
	__u8  type;
	__u8 *fp;
	int n;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	/* Initialize variables */
	fp = skb->data;
	n = 2;

	/* Get length, MSB first */
	len = be16_to_cpu(get_unaligned((__u16 *)(fp+n))); n += 2;

	IRDA_DEBUG(4, "%s(), len=%d\n", __FUNCTION__, len);

	/* Get object ID, MSB first */
	obj_id = be16_to_cpu(get_unaligned((__u16 *)(fp+n))); n += 2;

	type = fp[n++];
	IRDA_DEBUG(4, "%s(), Value type = %d\n", __FUNCTION__, type);

	switch (type) {
	case IAS_INTEGER:
		memcpy(&tmp_cpu32, fp+n, 4); n += 4;
		be32_to_cpus(&tmp_cpu32);
		value = irias_new_integer_value(tmp_cpu32);

		/*  Legal values restricted to 0x01-0x6f, page 15 irttp */
		IRDA_DEBUG(4, "%s(), lsap=%d\n", __FUNCTION__, value->t.integer);
		break;
	case IAS_STRING:
		charset = fp[n++];

		switch (charset) {
		case CS_ASCII:
			break;
/*		case CS_ISO_8859_1: */
/*		case CS_ISO_8859_2: */
/*		case CS_ISO_8859_3: */
/*		case CS_ISO_8859_4: */
/*		case CS_ISO_8859_5: */
/*		case CS_ISO_8859_6: */
/*		case CS_ISO_8859_7: */
/*		case CS_ISO_8859_8: */
/*		case CS_ISO_8859_9: */
/*		case CS_UNICODE: */
		default:
			IRDA_DEBUG(0, "%s(), charset %s, not supported\n",
				   __FUNCTION__, ias_charset_types[charset]);

			/* Aborting, close connection! */
			iriap_disconnect_request(self);
			return;
			/* break; */
		}
		value_len = fp[n++];
		IRDA_DEBUG(4, "%s(), strlen=%d\n", __FUNCTION__, value_len);

		/* Make sure the string is null-terminated */
		fp[n+value_len] = 0x00;
		IRDA_DEBUG(4, "Got string %s\n", fp+n);

		/* Will truncate to IAS_MAX_STRING bytes */
		value = irias_new_string_value(fp+n);
		break;
	case IAS_OCT_SEQ:
		value_len = be16_to_cpu(get_unaligned((__u16 *)(fp+n)));
		n += 2;

		/* Will truncate to IAS_MAX_OCTET_STRING bytes */
		value = irias_new_octseq_value(fp+n, value_len);
		break;
	default:
		value = irias_new_missing_value();
		break;
	}

	/* Finished, close connection! */
	iriap_disconnect_request(self);

	/* Warning, the client might close us, so remember no to use self
	 * anymore after calling confirm
	 */
	if (self->confirm)
		self->confirm(IAS_SUCCESS, obj_id, value, self->priv);
	else {
		IRDA_DEBUG(0, "%s(), missing handler!\n", __FUNCTION__);
		irias_delete_value(value);
	}
}

/*
 * Function iriap_getvaluebyclass_response ()
 *
 *    Send answer back to remote LM-IAS
 *
 */
static void iriap_getvaluebyclass_response(struct iriap_cb *self,
					   __u16 obj_id,
					   __u8 ret_code,
					   struct ias_value *value)
{
	struct sk_buff *tx_skb;
	int n;
	__u32 tmp_be32;
	__be16 tmp_be16;
	__u8 *fp;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);
	IRDA_ASSERT(value != NULL, return;);
	IRDA_ASSERT(value->len <= 1024, return;);

	/* Initialize variables */
	n = 0;

	/*
	 *  We must adjust the size of the response after the length of the
	 *  value. We add 32 bytes because of the 6 bytes for the frame and
	 *  max 5 bytes for the value coding.
	 */
	tx_skb = dev_alloc_skb(value->len + self->max_header_size + 32);
	if (!tx_skb)
		return;

	/* Reserve space for MUX and LAP header */
	skb_reserve(tx_skb, self->max_header_size);
	skb_put(tx_skb, 6);

	fp = tx_skb->data;

	/* Build frame */
	fp[n++] = GET_VALUE_BY_CLASS | IAP_LST;
	fp[n++] = ret_code;

	/* Insert list length (MSB first) */
	tmp_be16 = __constant_htons(0x0001);
	memcpy(fp+n, &tmp_be16, 2);  n += 2;

	/* Insert object identifier ( MSB first) */
	tmp_be16 = cpu_to_be16(obj_id);
	memcpy(fp+n, &tmp_be16, 2); n += 2;

	switch (value->type) {
	case IAS_STRING:
		skb_put(tx_skb, 3 + value->len);
		fp[n++] = value->type;
		fp[n++] = 0; /* ASCII */
		fp[n++] = (__u8) value->len;
		memcpy(fp+n, value->t.string, value->len); n+=value->len;
		break;
	case IAS_INTEGER:
		skb_put(tx_skb, 5);
		fp[n++] = value->type;

		tmp_be32 = cpu_to_be32(value->t.integer);
		memcpy(fp+n, &tmp_be32, 4); n += 4;
		break;
	case IAS_OCT_SEQ:
		skb_put(tx_skb, 3 + value->len);
		fp[n++] = value->type;

		tmp_be16 = cpu_to_be16(value->len);
		memcpy(fp+n, &tmp_be16, 2); n += 2;
		memcpy(fp+n, value->t.oct_seq, value->len); n+=value->len;
		break;
	case IAS_MISSING:
		IRDA_DEBUG( 3, "%s: sending IAS_MISSING\n", __FUNCTION__);
		skb_put(tx_skb, 1);
		fp[n++] = value->type;
		break;
	default:
		IRDA_DEBUG(0, "%s(), type not implemented!\n", __FUNCTION__);
		break;
	}
	iriap_do_r_connect_event(self, IAP_CALL_RESPONSE, tx_skb);

	/* Drop reference count - see state_r_execute(). */
	dev_kfree_skb(tx_skb);
}

/*
 * Function iriap_getvaluebyclass_indication (self, skb)
 *
 *    getvaluebyclass is requested from peer LM-IAS
 *
 */
static void iriap_getvaluebyclass_indication(struct iriap_cb *self,
					     struct sk_buff *skb)
{
	struct ias_object *obj;
	struct ias_attrib *attrib;
	int name_len;
	int attr_len;
	char name[IAS_MAX_CLASSNAME + 1];	/* 60 bytes */
	char attr[IAS_MAX_ATTRIBNAME + 1];	/* 60 bytes */
	__u8 *fp;
	int n;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	fp = skb->data;
	n = 1;

	name_len = fp[n++];
	memcpy(name, fp+n, name_len); n+=name_len;
	name[name_len] = '\0';

	attr_len = fp[n++];
	memcpy(attr, fp+n, attr_len); n+=attr_len;
	attr[attr_len] = '\0';

	IRDA_DEBUG(4, "LM-IAS: Looking up %s: %s\n", name, attr);
	obj = irias_find_object(name);

	if (obj == NULL) {
		IRDA_DEBUG(2, "LM-IAS: Object %s not found\n", name);
		iriap_getvaluebyclass_response(self, 0x1235, IAS_CLASS_UNKNOWN,
					       &irias_missing);
		return;
	}
	IRDA_DEBUG(4, "LM-IAS: found %s, id=%d\n", obj->name, obj->id);

	attrib = irias_find_attrib(obj, attr);
	if (attrib == NULL) {
		IRDA_DEBUG(2, "LM-IAS: Attribute %s not found\n", attr);
		iriap_getvaluebyclass_response(self, obj->id,
					       IAS_ATTRIB_UNKNOWN, 
					       &irias_missing);
		return;
	}

	/* We have a match; send the value.  */
	iriap_getvaluebyclass_response(self, obj->id, IAS_SUCCESS,
				       attrib->value);

	return;
}

/*
 * Function iriap_send_ack (void)
 *
 *    Currently not used
 *
 */
void iriap_send_ack(struct iriap_cb *self)
{
	struct sk_buff *tx_skb;
	__u8 *frame;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	tx_skb = dev_alloc_skb(64);
	if (!tx_skb)
		return;

	/* Reserve space for MUX and LAP header */
	skb_reserve(tx_skb, self->max_header_size);
	skb_put(tx_skb, 1);
	frame = tx_skb->data;

	/* Build frame */
	frame[0] = IAP_LST | IAP_ACK | self->operation;

	irlmp_data_request(self->lsap, tx_skb);
}

void iriap_connect_request(struct iriap_cb *self)
{
	int ret;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	ret = irlmp_connect_request(self->lsap, LSAP_IAS,
				    self->saddr, self->daddr,
				    NULL, NULL);
	if (ret < 0) {
		IRDA_DEBUG(0, "%s(), connect failed!\n", __FUNCTION__);
		self->confirm(IAS_DISCONNECT, 0, NULL, self->priv);
	}
}

/*
 * Function iriap_connect_confirm (handle, skb)
 *
 *    LSAP connection confirmed!
 *
 */
static void iriap_connect_confirm(void *instance, void *sap,
				  struct qos_info *qos, __u32 max_seg_size,
				  __u8 max_header_size,
				  struct sk_buff *skb)
{
	struct iriap_cb *self;

	self = (struct iriap_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	self->max_data_size = max_seg_size;
	self->max_header_size = max_header_size;

	del_timer(&self->watchdog_timer);

	iriap_do_client_event(self, IAP_LM_CONNECT_CONFIRM, skb);

	/* Drop reference count - see state_s_make_call(). */
	dev_kfree_skb(skb);
}

/*
 * Function iriap_connect_indication ( handle, skb)
 *
 *    Remote LM-IAS is requesting connection
 *
 */
static void iriap_connect_indication(void *instance, void *sap,
				     struct qos_info *qos, __u32 max_seg_size,
				     __u8 max_header_size,
				     struct sk_buff *skb)
{
	struct iriap_cb *self, *new;

	IRDA_DEBUG(1, "%s()\n", __FUNCTION__);

	self = (struct iriap_cb *) instance;

	IRDA_ASSERT(skb != NULL, return;);
	IRDA_ASSERT(self != NULL, goto out;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, goto out;);

	/* Start new server */
	new = iriap_open(LSAP_IAS, IAS_SERVER, NULL, NULL);
	if (!new) {
		IRDA_DEBUG(0, "%s(), open failed\n", __FUNCTION__);
		goto out;
	}

	/* Now attach up the new "socket" */
	new->lsap = irlmp_dup(self->lsap, new);
	if (!new->lsap) {
		IRDA_DEBUG(0, "%s(), dup failed!\n", __FUNCTION__);
		goto out;
	}

	new->max_data_size = max_seg_size;
	new->max_header_size = max_header_size;

	/* Clean up the original one to keep it in listen state */
	irlmp_listen(self->lsap);

	iriap_do_server_event(new, IAP_LM_CONNECT_INDICATION, skb);

out:
	/* Drop reference count - see state_r_disconnect(). */
	dev_kfree_skb(skb);
}

/*
 * Function iriap_data_indication (handle, skb)
 *
 *    Receives data from connection identified by handle from IrLMP
 *
 */
static int iriap_data_indication(void *instance, void *sap,
				 struct sk_buff *skb)
{
	struct iriap_cb *self;
	__u8  *frame;
	__u8  opcode;

	IRDA_DEBUG(3, "%s()\n", __FUNCTION__);

	self = (struct iriap_cb *) instance;

	IRDA_ASSERT(skb != NULL, return 0;);
	IRDA_ASSERT(self != NULL, goto out;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, goto out;);

	frame = skb->data;

	if (self->mode == IAS_SERVER) {
		/* Call server */
		IRDA_DEBUG(4, "%s(), Calling server!\n", __FUNCTION__);
		iriap_do_r_connect_event(self, IAP_RECV_F_LST, skb);
		goto out;
	}
	opcode = frame[0];
	if (~opcode & IAP_LST) {
		IRDA_WARNING("%s:, IrIAS multiframe commands or "
			     "results is not implemented yet!\n",
			     __FUNCTION__);
		goto out;
	}

	/* Check for ack frames since they don't contain any data */
	if (opcode & IAP_ACK) {
		IRDA_DEBUG(0, "%s() Got ack frame!\n", __FUNCTION__);
		goto out;
	}

	opcode &= ~IAP_LST; /* Mask away LST bit */

	switch (opcode) {
	case GET_INFO_BASE:
		IRDA_DEBUG(0, "IrLMP GetInfoBaseDetails not implemented!\n");
		break;
	case GET_VALUE_BY_CLASS:
		iriap_do_call_event(self, IAP_RECV_F_LST, NULL);

		switch (frame[1]) {
		case IAS_SUCCESS:
			iriap_getvaluebyclass_confirm(self, skb);
			break;
		case IAS_CLASS_UNKNOWN:
			IRDA_DEBUG(1, "%s(), No such class!\n", __FUNCTION__);
			/* Finished, close connection! */
			iriap_disconnect_request(self);

			/*
			 * Warning, the client might close us, so remember
			 * no to use self anymore after calling confirm
			 */
			if (self->confirm)
				self->confirm(IAS_CLASS_UNKNOWN, 0, NULL,
					      self->priv);
			break;
		case IAS_ATTRIB_UNKNOWN:
			IRDA_DEBUG(1, "%s(), No such attribute!\n", __FUNCTION__);
			/* Finished, close connection! */
			iriap_disconnect_request(self);

			/*
			 * Warning, the client might close us, so remember
			 * no to use self anymore after calling confirm
			 */
			if (self->confirm)
				self->confirm(IAS_ATTRIB_UNKNOWN, 0, NULL,
					      self->priv);
			break;
		}
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown op-code: %02x\n", __FUNCTION__,
			   opcode);
		break;
	}

out:
	/* Cleanup - sub-calls will have done skb_get() as needed. */
	dev_kfree_skb(skb);
	return 0;
}

/*
 * Function iriap_call_indication (self, skb)
 *
 *    Received call to server from peer LM-IAS
 *
 */
void iriap_call_indication(struct iriap_cb *self, struct sk_buff *skb)
{
	__u8 *fp;
	__u8 opcode;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	fp = skb->data;

	opcode = fp[0];
	if (~opcode & 0x80) {
		IRDA_WARNING("%s: IrIAS multiframe commands or results"
			     "is not implemented yet!\n", __FUNCTION__);
		return;
	}
	opcode &= 0x7f; /* Mask away LST bit */

	switch (opcode) {
	case GET_INFO_BASE:
		IRDA_WARNING("%s: GetInfoBaseDetails not implemented yet!\n",
			     __FUNCTION__);
		break;
	case GET_VALUE_BY_CLASS:
		iriap_getvaluebyclass_indication(self, skb);
		break;
	}
	/* skb will be cleaned up in iriap_data_indication */
}

/*
 * Function iriap_watchdog_timer_expired (data)
 *
 *    Query has taken too long time, so abort
 *
 */
static void iriap_watchdog_timer_expired(void *data)
{
	struct iriap_cb *self = (struct iriap_cb *) data;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IAS_MAGIC, return;);

	/* iriap_close(self); */
}

#ifdef CONFIG_PROC_FS

static const char *ias_value_types[] = {
	"IAS_MISSING",
	"IAS_INTEGER",
	"IAS_OCT_SEQ",
	"IAS_STRING"
};

static inline struct ias_object *irias_seq_idx(loff_t pos) 
{
	struct ias_object *obj;

	for (obj = (struct ias_object *) hashbin_get_first(irias_objects);
	     obj; obj = (struct ias_object *) hashbin_get_next(irias_objects)) {
		if (pos-- == 0)
			break;
	}
		
	return obj;
}

static void *irias_seq_start(struct seq_file *seq, loff_t *pos)
{
	spin_lock_irq(&irias_objects->hb_spinlock);

	return *pos ? irias_seq_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *irias_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return (v == SEQ_START_TOKEN) 
		? (void *) hashbin_get_first(irias_objects)
		: (void *) hashbin_get_next(irias_objects);
}

static void irias_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock_irq(&irias_objects->hb_spinlock);
}

static int irias_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "LM-IAS Objects:\n");
	else {
		struct ias_object *obj = v;
		struct ias_attrib *attrib;

		IRDA_ASSERT(obj->magic == IAS_OBJECT_MAGIC, return -EINVAL;);

		seq_printf(seq, "name: %s, id=%d\n",
			   obj->name, obj->id);

		/* Careful for priority inversions here !
		 * All other uses of attrib spinlock are independent of
		 * the object spinlock, so we are safe. Jean II */
		spin_lock(&obj->attribs->hb_spinlock);

		/* List all attributes for this object */
		for (attrib = (struct ias_attrib *) hashbin_get_first(obj->attribs);
		     attrib != NULL;
		     attrib = (struct ias_attrib *) hashbin_get_next(obj->attribs)) {
		     
			IRDA_ASSERT(attrib->magic == IAS_ATTRIB_MAGIC,
				    goto outloop; );

			seq_printf(seq, " - Attribute name: \"%s\", ",
				   attrib->name);
			seq_printf(seq, "value[%s]: ",
				   ias_value_types[attrib->value->type]);

			switch (attrib->value->type) {
			case IAS_INTEGER:
				seq_printf(seq, "%d\n",
					   attrib->value->t.integer);
				break;
			case IAS_STRING:
				seq_printf(seq, "\"%s\"\n",
					   attrib->value->t.string);
				break;
			case IAS_OCT_SEQ:
				seq_printf(seq, "octet sequence (%d bytes)\n", 
					   attrib->value->len);
				break;
			case IAS_MISSING:
				seq_puts(seq, "missing\n");
				break;
			default:
				seq_printf(seq, "type %d?\n", 
					   attrib->value->type);
			}
			seq_putc(seq, '\n');

		}
	IRDA_ASSERT_LABEL(outloop:)
		spin_unlock(&obj->attribs->hb_spinlock);
	}

	return 0;
}

static struct seq_operations irias_seq_ops = {
	.start  = irias_seq_start,
	.next   = irias_seq_next,
	.stop   = irias_seq_stop,
	.show   = irias_seq_show,
};

static int irias_seq_open(struct inode *inode, struct file *file)
{
	IRDA_ASSERT( irias_objects != NULL, return -EINVAL;);

	return seq_open(file, &irias_seq_ops);
}

struct file_operations irias_seq_fops = {
	.owner		= THIS_MODULE,
	.open           = irias_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release,
};

#endif /* PROC_FS */
