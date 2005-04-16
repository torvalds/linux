/* 
   CMTP implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2002-2003 Marcel Holtmann <marcel@holtmann.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/ioctl.h>
#include <linux/file.h>
#include <linux/wait.h>
#include <net/sock.h>

#include <linux/isdn/capilli.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>

#include "cmtp.h"

#ifndef CONFIG_BT_CMTP_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#define CAPI_INTEROPERABILITY		0x20

#define CAPI_INTEROPERABILITY_REQ	CAPICMD(CAPI_INTEROPERABILITY, CAPI_REQ)
#define CAPI_INTEROPERABILITY_CONF	CAPICMD(CAPI_INTEROPERABILITY, CAPI_CONF)
#define CAPI_INTEROPERABILITY_IND	CAPICMD(CAPI_INTEROPERABILITY, CAPI_IND)
#define CAPI_INTEROPERABILITY_RESP	CAPICMD(CAPI_INTEROPERABILITY, CAPI_RESP)

#define CAPI_INTEROPERABILITY_REQ_LEN	(CAPI_MSG_BASELEN + 2)
#define CAPI_INTEROPERABILITY_CONF_LEN	(CAPI_MSG_BASELEN + 4)
#define CAPI_INTEROPERABILITY_IND_LEN	(CAPI_MSG_BASELEN + 2)
#define CAPI_INTEROPERABILITY_RESP_LEN	(CAPI_MSG_BASELEN + 2)

#define CAPI_FUNCTION_REGISTER		0
#define CAPI_FUNCTION_RELEASE		1
#define CAPI_FUNCTION_GET_PROFILE	2
#define CAPI_FUNCTION_GET_MANUFACTURER	3
#define CAPI_FUNCTION_GET_VERSION	4
#define CAPI_FUNCTION_GET_SERIAL_NUMBER	5
#define CAPI_FUNCTION_MANUFACTURER	6
#define CAPI_FUNCTION_LOOPBACK		7


#define CMTP_MSGNUM	1
#define CMTP_APPLID	2
#define CMTP_MAPPING	3

static struct cmtp_application *cmtp_application_add(struct cmtp_session *session, __u16 appl)
{
	struct cmtp_application *app = kmalloc(sizeof(*app), GFP_KERNEL);

	BT_DBG("session %p application %p appl %d", session, app, appl);

	if (!app)
		return NULL;

	memset(app, 0, sizeof(*app));

	app->state = BT_OPEN;
	app->appl = appl;

	list_add_tail(&app->list, &session->applications);

	return app;
}

static void cmtp_application_del(struct cmtp_session *session, struct cmtp_application *app)
{
	BT_DBG("session %p application %p", session, app);

	if (app) {
		list_del(&app->list);
		kfree(app);
	}
}

static struct cmtp_application *cmtp_application_get(struct cmtp_session *session, int pattern, __u16 value)
{
	struct cmtp_application *app;
	struct list_head *p, *n;

	list_for_each_safe(p, n, &session->applications) {
		app = list_entry(p, struct cmtp_application, list);
		switch (pattern) {
		case CMTP_MSGNUM:
			if (app->msgnum == value)
				return app;
			break;
		case CMTP_APPLID:
			if (app->appl == value)
				return app;
			break;
		case CMTP_MAPPING:
			if (app->mapping == value)
				return app;
			break;
		}
	}

	return NULL;
}

static int cmtp_msgnum_get(struct cmtp_session *session)
{
	session->msgnum++;

	if ((session->msgnum & 0xff) > 200)
		session->msgnum = CMTP_INITIAL_MSGNUM + 1;

	return session->msgnum;
}

static void cmtp_send_capimsg(struct cmtp_session *session, struct sk_buff *skb)
{
	struct cmtp_scb *scb = (void *) skb->cb;

	BT_DBG("session %p skb %p len %d", session, skb, skb->len);

	scb->id = -1;
	scb->data = (CAPIMSG_COMMAND(skb->data) == CAPI_DATA_B3);

	skb_queue_tail(&session->transmit, skb);

	cmtp_schedule(session);
}

static void cmtp_send_interopmsg(struct cmtp_session *session,
					__u8 subcmd, __u16 appl, __u16 msgnum,
					__u16 function, unsigned char *buf, int len)
{
	struct sk_buff *skb;
	unsigned char *s;

	BT_DBG("session %p subcmd 0x%02x appl %d msgnum %d", session, subcmd, appl, msgnum);

	if (!(skb = alloc_skb(CAPI_MSG_BASELEN + 6 + len, GFP_ATOMIC))) {
		BT_ERR("Can't allocate memory for interoperability packet");
		return;
	}

	s = skb_put(skb, CAPI_MSG_BASELEN + 6 + len);

	capimsg_setu16(s, 0, CAPI_MSG_BASELEN + 6 + len);
	capimsg_setu16(s, 2, appl);
	capimsg_setu8 (s, 4, CAPI_INTEROPERABILITY);
	capimsg_setu8 (s, 5, subcmd);
	capimsg_setu16(s, 6, msgnum);

	/* Interoperability selector (Bluetooth Device Management) */
	capimsg_setu16(s, 8, 0x0001);

	capimsg_setu8 (s, 10, 3 + len);
	capimsg_setu16(s, 11, function);
	capimsg_setu8 (s, 13, len);

	if (len > 0)
		memcpy(s + 14, buf, len);

	cmtp_send_capimsg(session, skb);
}

static void cmtp_recv_interopmsg(struct cmtp_session *session, struct sk_buff *skb)
{
	struct capi_ctr *ctrl = &session->ctrl;
	struct cmtp_application *application;
	__u16 appl, msgnum, func, info;
	__u32 controller;

	BT_DBG("session %p skb %p len %d", session, skb, skb->len);

	switch (CAPIMSG_SUBCOMMAND(skb->data)) {
	case CAPI_CONF:
		func = CAPIMSG_U16(skb->data, CAPI_MSG_BASELEN + 5);
		info = CAPIMSG_U16(skb->data, CAPI_MSG_BASELEN + 8);

		switch (func) {
		case CAPI_FUNCTION_REGISTER:
			msgnum = CAPIMSG_MSGID(skb->data);

			application = cmtp_application_get(session, CMTP_MSGNUM, msgnum);
			if (application) {
				application->state = BT_CONNECTED;
				application->msgnum = 0;
				application->mapping = CAPIMSG_APPID(skb->data);
				wake_up_interruptible(&session->wait);
			}

			break;

		case CAPI_FUNCTION_RELEASE:
			appl = CAPIMSG_APPID(skb->data);

			application = cmtp_application_get(session, CMTP_MAPPING, appl);
			if (application) {
				application->state = BT_CLOSED;
				application->msgnum = 0;
				wake_up_interruptible(&session->wait);
			}

			break;

		case CAPI_FUNCTION_GET_PROFILE:
			controller = CAPIMSG_U16(skb->data, CAPI_MSG_BASELEN + 11);
			msgnum = CAPIMSG_MSGID(skb->data);

			if (!info && (msgnum == CMTP_INITIAL_MSGNUM)) {
				session->ncontroller = controller;
				wake_up_interruptible(&session->wait);
				break;
			}

			if (!info && ctrl) {
				memcpy(&ctrl->profile,
					skb->data + CAPI_MSG_BASELEN + 11,
					sizeof(capi_profile));
				session->state = BT_CONNECTED;
				capi_ctr_ready(ctrl);
			}

			break;

		case CAPI_FUNCTION_GET_MANUFACTURER:
			controller = CAPIMSG_U32(skb->data, CAPI_MSG_BASELEN + 10);

			if (!info && ctrl) {
				strncpy(ctrl->manu,
					skb->data + CAPI_MSG_BASELEN + 15,
					skb->data[CAPI_MSG_BASELEN + 14]);
			}

			break;

		case CAPI_FUNCTION_GET_VERSION:
			controller = CAPIMSG_U32(skb->data, CAPI_MSG_BASELEN + 12);

			if (!info && ctrl) {
				ctrl->version.majorversion = CAPIMSG_U32(skb->data, CAPI_MSG_BASELEN + 16);
				ctrl->version.minorversion = CAPIMSG_U32(skb->data, CAPI_MSG_BASELEN + 20);
				ctrl->version.majormanuversion = CAPIMSG_U32(skb->data, CAPI_MSG_BASELEN + 24);
				ctrl->version.minormanuversion = CAPIMSG_U32(skb->data, CAPI_MSG_BASELEN + 28);
			}

			break;

		case CAPI_FUNCTION_GET_SERIAL_NUMBER:
			controller = CAPIMSG_U32(skb->data, CAPI_MSG_BASELEN + 12);

			if (!info && ctrl) {
				memset(ctrl->serial, 0, CAPI_SERIAL_LEN);
				strncpy(ctrl->serial,
					skb->data + CAPI_MSG_BASELEN + 17,
					skb->data[CAPI_MSG_BASELEN + 16]);
			}

			break;
		}

		break;

	case CAPI_IND:
		func = CAPIMSG_U16(skb->data, CAPI_MSG_BASELEN + 3);

		if (func == CAPI_FUNCTION_LOOPBACK) {
			appl = CAPIMSG_APPID(skb->data);
			msgnum = CAPIMSG_MSGID(skb->data);
			cmtp_send_interopmsg(session, CAPI_RESP, appl, msgnum, func,
						skb->data + CAPI_MSG_BASELEN + 6,
						skb->data[CAPI_MSG_BASELEN + 5]);
		}

		break;
	}

	kfree_skb(skb);
}

void cmtp_recv_capimsg(struct cmtp_session *session, struct sk_buff *skb)
{
	struct capi_ctr *ctrl = &session->ctrl;
	struct cmtp_application *application;
	__u16 cmd, appl;
	__u32 contr;

	BT_DBG("session %p skb %p len %d", session, skb, skb->len);

	if (CAPIMSG_COMMAND(skb->data) == CAPI_INTEROPERABILITY) {
		cmtp_recv_interopmsg(session, skb);
		return;
	}

	if (session->flags & (1 << CMTP_LOOPBACK)) {
		kfree_skb(skb);
		return;
	}

	cmd = CAPICMD(CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data));
	appl = CAPIMSG_APPID(skb->data);
	contr = CAPIMSG_CONTROL(skb->data);

	application = cmtp_application_get(session, CMTP_MAPPING, appl);
	if (application) {
		appl = application->appl;
		CAPIMSG_SETAPPID(skb->data, appl);
	} else {
		BT_ERR("Can't find application with id %d", appl);
		kfree_skb(skb);
		return;
	}

	if ((contr & 0x7f) == 0x01) {
		contr = (contr & 0xffffff80) | session->num;
		CAPIMSG_SETCONTROL(skb->data, contr);
	}

	if (!ctrl) {
		BT_ERR("Can't find controller %d for message", session->num);
		kfree_skb(skb);
		return;
	}

	capi_ctr_handle_message(ctrl, appl, skb);
}

static int cmtp_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	BT_DBG("ctrl %p data %p", ctrl, data);

	return 0;
}

static void cmtp_reset_ctr(struct capi_ctr *ctrl)
{
	struct cmtp_session *session = ctrl->driverdata;

	BT_DBG("ctrl %p", ctrl);

	capi_ctr_reseted(ctrl);

	atomic_inc(&session->terminate);
	cmtp_schedule(session);
}

static void cmtp_register_appl(struct capi_ctr *ctrl, __u16 appl, capi_register_params *rp)
{
	DECLARE_WAITQUEUE(wait, current);
	struct cmtp_session *session = ctrl->driverdata;
	struct cmtp_application *application;
	unsigned long timeo = CMTP_INTEROP_TIMEOUT;
	unsigned char buf[8];
	int err = 0, nconn, want = rp->level3cnt;

	BT_DBG("ctrl %p appl %d level3cnt %d datablkcnt %d datablklen %d",
		ctrl, appl, rp->level3cnt, rp->datablkcnt, rp->datablklen);

	application = cmtp_application_add(session, appl);
	if (!application) {
		BT_ERR("Can't allocate memory for new application");
		return;
	}

	if (want < 0)
		nconn = ctrl->profile.nbchannel * -want;
	else
		nconn = want;

	if (nconn == 0)
		nconn = ctrl->profile.nbchannel;

	capimsg_setu16(buf, 0, nconn);
	capimsg_setu16(buf, 2, rp->datablkcnt);
	capimsg_setu16(buf, 4, rp->datablklen);

	application->state = BT_CONFIG;
	application->msgnum = cmtp_msgnum_get(session);

	cmtp_send_interopmsg(session, CAPI_REQ, 0x0000, application->msgnum,
				CAPI_FUNCTION_REGISTER, buf, 6);

	add_wait_queue(&session->wait, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		if (application->state == BT_CLOSED) {
			err = -application->err;
			break;
		}

		if (application->state == BT_CONNECTED)
			break;

		if (signal_pending(current)) {
			err = -EINTR;
			break;
		}

		timeo = schedule_timeout(timeo);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&session->wait, &wait);

	if (err) {
		cmtp_application_del(session, application);
		return;
	}
}

static void cmtp_release_appl(struct capi_ctr *ctrl, __u16 appl)
{
	struct cmtp_session *session = ctrl->driverdata;
	struct cmtp_application *application;

	BT_DBG("ctrl %p appl %d", ctrl, appl);

	application = cmtp_application_get(session, CMTP_APPLID, appl);
	if (!application) {
		BT_ERR("Can't find application");
		return;
	}

	application->msgnum = cmtp_msgnum_get(session);

	cmtp_send_interopmsg(session, CAPI_REQ, application->mapping, application->msgnum,
				CAPI_FUNCTION_RELEASE, NULL, 0);

	wait_event_interruptible_timeout(session->wait,
			(application->state == BT_CLOSED), CMTP_INTEROP_TIMEOUT);

	cmtp_application_del(session, application);
}

static u16 cmtp_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	struct cmtp_session *session = ctrl->driverdata;
	struct cmtp_application *application;
	__u16 appl;
	__u32 contr;

	BT_DBG("ctrl %p skb %p", ctrl, skb);

	appl = CAPIMSG_APPID(skb->data);
	contr = CAPIMSG_CONTROL(skb->data);

	application = cmtp_application_get(session, CMTP_APPLID, appl);
	if ((!application) || (application->state != BT_CONNECTED)) {
		BT_ERR("Can't find application with id %d", appl);
		return CAPI_ILLAPPNR;
	}

	CAPIMSG_SETAPPID(skb->data, application->mapping);

	if ((contr & 0x7f) == session->num) {
		contr = (contr & 0xffffff80) | 0x01;
		CAPIMSG_SETCONTROL(skb->data, contr);
	}

	cmtp_send_capimsg(session, skb);

	return CAPI_NOERROR;
}

static char *cmtp_procinfo(struct capi_ctr *ctrl)
{
	return "CAPI Message Transport Protocol";
}

static int cmtp_ctr_read_proc(char *page, char **start, off_t off, int count, int *eof, struct capi_ctr *ctrl)
{
	struct cmtp_session *session = ctrl->driverdata;
	struct cmtp_application *app;
	struct list_head *p, *n;
	int len = 0;

	len += sprintf(page + len, "%s\n\n", cmtp_procinfo(ctrl));
	len += sprintf(page + len, "addr %s\n", session->name);
	len += sprintf(page + len, "ctrl %d\n", session->num);

	list_for_each_safe(p, n, &session->applications) {
		app = list_entry(p, struct cmtp_application, list);
		len += sprintf(page + len, "appl %d -> %d\n", app->appl, app->mapping);
	}

	if (off + count >= len)
		*eof = 1;

	if (len < off)
		return 0;

	*start = page + off;

	return ((count < len - off) ? count : len - off);
}


int cmtp_attach_device(struct cmtp_session *session)
{
	unsigned char buf[4];
	long ret;

	BT_DBG("session %p", session);

	capimsg_setu32(buf, 0, 0);

	cmtp_send_interopmsg(session, CAPI_REQ, 0xffff, CMTP_INITIAL_MSGNUM,
				CAPI_FUNCTION_GET_PROFILE, buf, 4);

	ret = wait_event_interruptible_timeout(session->wait,
			session->ncontroller, CMTP_INTEROP_TIMEOUT);
	
	BT_INFO("Found %d CAPI controller(s) on device %s", session->ncontroller, session->name);

	if (!ret)
		return -ETIMEDOUT;

	if (!session->ncontroller)
		return -ENODEV;

	if (session->ncontroller > 1)
		BT_INFO("Setting up only CAPI controller 1");

	session->ctrl.owner      = THIS_MODULE;
	session->ctrl.driverdata = session;
	strcpy(session->ctrl.name, session->name);

	session->ctrl.driver_name   = "cmtp";
	session->ctrl.load_firmware = cmtp_load_firmware;
	session->ctrl.reset_ctr     = cmtp_reset_ctr;
	session->ctrl.register_appl = cmtp_register_appl;
	session->ctrl.release_appl  = cmtp_release_appl;
	session->ctrl.send_message  = cmtp_send_message;

	session->ctrl.procinfo      = cmtp_procinfo;
	session->ctrl.ctr_read_proc = cmtp_ctr_read_proc;

	if (attach_capi_ctr(&session->ctrl) < 0) {
		BT_ERR("Can't attach new controller");
		return -EBUSY;
	}

	session->num = session->ctrl.cnr;

	BT_DBG("session %p num %d", session, session->num);

	capimsg_setu32(buf, 0, 1);

	cmtp_send_interopmsg(session, CAPI_REQ, 0xffff, cmtp_msgnum_get(session),
				CAPI_FUNCTION_GET_MANUFACTURER, buf, 4);

	cmtp_send_interopmsg(session, CAPI_REQ, 0xffff, cmtp_msgnum_get(session),
				CAPI_FUNCTION_GET_VERSION, buf, 4);

	cmtp_send_interopmsg(session, CAPI_REQ, 0xffff, cmtp_msgnum_get(session),
				CAPI_FUNCTION_GET_SERIAL_NUMBER, buf, 4);

	cmtp_send_interopmsg(session, CAPI_REQ, 0xffff, cmtp_msgnum_get(session),
				CAPI_FUNCTION_GET_PROFILE, buf, 4);

	return 0;
}

void cmtp_detach_device(struct cmtp_session *session)
{
	BT_DBG("session %p", session);

	detach_capi_ctr(&session->ctrl);
}
