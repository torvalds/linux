/*-
 * Copyright (c) 2014,2016-2017 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *	Author:	Sainath Varanasi.
 *	Date:	4/2012
 *	Email:	bsdic@microsoft.com
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/reboot.h>
#include <sys/lock.h>
#include <sys/taskqueue.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/un.h>
#include <sys/endian.h>
#include <sys/_null.h>
#include <sys/sema.h>
#include <sys/signal.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/mutex.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/vmbus.h>
#include <dev/hyperv/utilities/hv_utilreg.h>
#include <dev/hyperv/utilities/vmbus_icreg.h>
#include <dev/hyperv/utilities/vmbus_icvar.h>

#include "unicode.h"
#include "hv_kvp.h"
#include "vmbus_if.h"

/* hv_kvp defines */
#define BUFFERSIZE	sizeof(struct hv_kvp_msg)
#define kvp_hdr		hdr.kvp_hdr

#define KVP_FWVER_MAJOR		3
#define KVP_FWVER		VMBUS_IC_VERSION(KVP_FWVER_MAJOR, 0)

#define KVP_MSGVER_MAJOR	4
#define KVP_MSGVER		VMBUS_IC_VERSION(KVP_MSGVER_MAJOR, 0)

/* hv_kvp debug control */
static int hv_kvp_log = 0;

#define	hv_kvp_log_error(...)	do {				\
	if (hv_kvp_log > 0)				\
		log(LOG_ERR, "hv_kvp: " __VA_ARGS__);	\
} while (0)

#define	hv_kvp_log_info(...) do {				\
	if (hv_kvp_log > 1)				\
		log(LOG_INFO, "hv_kvp: " __VA_ARGS__);		\
} while (0)

static const struct vmbus_ic_desc vmbus_kvp_descs[] = {
	{
		.ic_guid = { .hv_guid = {
		    0xe7, 0xf4, 0xa0, 0xa9, 0x45, 0x5a, 0x96, 0x4d,
		    0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x3,  0xe6 } },
		.ic_desc = "Hyper-V KVP"
	},
	VMBUS_IC_DESC_END
};

/* character device prototypes */
static d_open_t		hv_kvp_dev_open;
static d_close_t	hv_kvp_dev_close;
static d_read_t		hv_kvp_dev_daemon_read;
static d_write_t	hv_kvp_dev_daemon_write;
static d_poll_t		hv_kvp_dev_daemon_poll;

/* hv_kvp character device structure */
static struct cdevsw hv_kvp_cdevsw =
{
	.d_version	= D_VERSION,
	.d_open		= hv_kvp_dev_open,
	.d_close	= hv_kvp_dev_close,
	.d_read		= hv_kvp_dev_daemon_read,
	.d_write	= hv_kvp_dev_daemon_write,
	.d_poll		= hv_kvp_dev_daemon_poll,
	.d_name		= "hv_kvp_dev",
};


/*
 * Global state to track and synchronize multiple
 * KVP transaction requests from the host.
 */
typedef struct hv_kvp_sc {
	struct vmbus_ic_softc	util_sc;
	device_t		dev;

	/* Unless specified the pending mutex should be
	 * used to alter the values of the following parameters:
	 * 1. req_in_progress
	 * 2. req_timed_out
	 */
	struct mtx		pending_mutex;

	struct task		task;

	/* To track if transaction is active or not */
	boolean_t		req_in_progress;
	/* Tracks if daemon did not reply back in time */
	boolean_t		req_timed_out;
	/* Tracks if daemon is serving a request currently */
	boolean_t		daemon_busy;

	/* Length of host message */
	uint32_t		host_msg_len;

	/* Host message id */
	uint64_t		host_msg_id;

	/* Current kvp message from the host */
	struct hv_kvp_msg	*host_kvp_msg;

	 /* Current kvp message for daemon */
	struct hv_kvp_msg	daemon_kvp_msg;

	/* Rcv buffer for communicating with the host*/
	uint8_t			*rcv_buf;

	/* Device semaphore to control communication */
	struct sema		dev_sema;

	/* Indicates if daemon registered with driver */
	boolean_t		register_done;

	/* Character device status */
	boolean_t		dev_accessed;

	struct cdev *hv_kvp_dev;

	struct proc *daemon_task;

	struct selinfo hv_kvp_selinfo;
} hv_kvp_sc;

/* hv_kvp prototypes */
static int	hv_kvp_req_in_progress(hv_kvp_sc *sc);
static void	hv_kvp_transaction_init(hv_kvp_sc *sc, uint32_t, uint64_t, uint8_t *);
static void	hv_kvp_send_msg_to_daemon(hv_kvp_sc *sc);
static void	hv_kvp_process_request(void *context, int pending);

/*
 * hv_kvp low level functions
 */

/*
 * Check if kvp transaction is in progres
 */
static int
hv_kvp_req_in_progress(hv_kvp_sc *sc)
{

	return (sc->req_in_progress);
}


/*
 * This routine is called whenever a message is received from the host
 */
static void
hv_kvp_transaction_init(hv_kvp_sc *sc, uint32_t rcv_len,
			uint64_t request_id, uint8_t *rcv_buf)
{

	/* Store all the relevant message details in the global structure */
	/* Do not need to use mutex for req_in_progress here */
	sc->req_in_progress = true;
	sc->host_msg_len = rcv_len;
	sc->host_msg_id = request_id;
	sc->rcv_buf = rcv_buf;
	sc->host_kvp_msg = (struct hv_kvp_msg *)&rcv_buf[
	    sizeof(struct hv_vmbus_pipe_hdr) +
	    sizeof(struct hv_vmbus_icmsg_hdr)];
}

/*
 * Convert ip related info in umsg from utf8 to utf16 and store in hmsg
 */
static int
hv_kvp_convert_utf8_ipinfo_to_utf16(struct hv_kvp_msg *umsg,
				    struct hv_kvp_ip_msg *host_ip_msg)
{
	int err_ip, err_subnet, err_gway, err_dns, err_adap;
	int UNUSED_FLAG = 1;

	utf8_to_utf16((uint16_t *)host_ip_msg->kvp_ip_val.ip_addr,
	    MAX_IP_ADDR_SIZE,
	    (char *)umsg->body.kvp_ip_val.ip_addr,
	    strlen((char *)umsg->body.kvp_ip_val.ip_addr),
	    UNUSED_FLAG,
	    &err_ip);
	utf8_to_utf16((uint16_t *)host_ip_msg->kvp_ip_val.sub_net,
	    MAX_IP_ADDR_SIZE,
	    (char *)umsg->body.kvp_ip_val.sub_net,
	    strlen((char *)umsg->body.kvp_ip_val.sub_net),
	    UNUSED_FLAG,
	    &err_subnet);
	utf8_to_utf16((uint16_t *)host_ip_msg->kvp_ip_val.gate_way,
	    MAX_GATEWAY_SIZE,
	    (char *)umsg->body.kvp_ip_val.gate_way,
	    strlen((char *)umsg->body.kvp_ip_val.gate_way),
	    UNUSED_FLAG,
	    &err_gway);
	utf8_to_utf16((uint16_t *)host_ip_msg->kvp_ip_val.dns_addr,
	    MAX_IP_ADDR_SIZE,
	    (char *)umsg->body.kvp_ip_val.dns_addr,
	    strlen((char *)umsg->body.kvp_ip_val.dns_addr),
	    UNUSED_FLAG,
	    &err_dns);
	utf8_to_utf16((uint16_t *)host_ip_msg->kvp_ip_val.adapter_id,
	    MAX_ADAPTER_ID_SIZE,
	    (char *)umsg->body.kvp_ip_val.adapter_id,
	    strlen((char *)umsg->body.kvp_ip_val.adapter_id),
	    UNUSED_FLAG,
	    &err_adap);

	host_ip_msg->kvp_ip_val.dhcp_enabled = umsg->body.kvp_ip_val.dhcp_enabled;
	host_ip_msg->kvp_ip_val.addr_family = umsg->body.kvp_ip_val.addr_family;

	return (err_ip | err_subnet | err_gway | err_dns | err_adap);
}


/*
 * Convert ip related info in hmsg from utf16 to utf8 and store in umsg
 */
static int
hv_kvp_convert_utf16_ipinfo_to_utf8(struct hv_kvp_ip_msg *host_ip_msg,
				    struct hv_kvp_msg *umsg)
{
	int err_ip, err_subnet, err_gway, err_dns, err_adap;
	int UNUSED_FLAG = 1;
	device_t *devs;
	int devcnt;

	/* IP Address */
	utf16_to_utf8((char *)umsg->body.kvp_ip_val.ip_addr,
	    MAX_IP_ADDR_SIZE,
	    (uint16_t *)host_ip_msg->kvp_ip_val.ip_addr,
	    MAX_IP_ADDR_SIZE,
	    UNUSED_FLAG,
	    &err_ip);

	/* Adapter ID : GUID */
	utf16_to_utf8((char *)umsg->body.kvp_ip_val.adapter_id,
	    MAX_ADAPTER_ID_SIZE,
	    (uint16_t *)host_ip_msg->kvp_ip_val.adapter_id,
	    MAX_ADAPTER_ID_SIZE,
	    UNUSED_FLAG,
	    &err_adap);

	if (devclass_get_devices(devclass_find("hn"), &devs, &devcnt) == 0) {
		for (devcnt = devcnt - 1; devcnt >= 0; devcnt--) {
			device_t dev = devs[devcnt];
			struct vmbus_channel *chan;
			char buf[HYPERV_GUID_STRLEN];
			int n;

			chan = vmbus_get_channel(dev);
			n = hyperv_guid2str(vmbus_chan_guid_inst(chan), buf,
			    sizeof(buf));

			/*
			 * The string in the 'kvp_ip_val.adapter_id' has
			 * braces around the GUID; skip the leading brace
			 * in 'kvp_ip_val.adapter_id'.
			 */
			if (strncmp(buf,
			    ((char *)&umsg->body.kvp_ip_val.adapter_id) + 1,
			    n) == 0) {
				strlcpy((char *)umsg->body.kvp_ip_val.adapter_id,
				    device_get_nameunit(dev), MAX_ADAPTER_ID_SIZE);
				break;
			}
		}
		free(devs, M_TEMP);
	}

	/* Address Family , DHCP , SUBNET, Gateway, DNS */
	umsg->kvp_hdr.operation = host_ip_msg->operation;
	umsg->body.kvp_ip_val.addr_family = host_ip_msg->kvp_ip_val.addr_family;
	umsg->body.kvp_ip_val.dhcp_enabled = host_ip_msg->kvp_ip_val.dhcp_enabled;
	utf16_to_utf8((char *)umsg->body.kvp_ip_val.sub_net, MAX_IP_ADDR_SIZE,
	    (uint16_t *)host_ip_msg->kvp_ip_val.sub_net,
	    MAX_IP_ADDR_SIZE,
	    UNUSED_FLAG,
	    &err_subnet);

	utf16_to_utf8((char *)umsg->body.kvp_ip_val.gate_way, MAX_GATEWAY_SIZE,
	    (uint16_t *)host_ip_msg->kvp_ip_val.gate_way,
	    MAX_GATEWAY_SIZE,
	    UNUSED_FLAG,
	    &err_gway);

	utf16_to_utf8((char *)umsg->body.kvp_ip_val.dns_addr, MAX_IP_ADDR_SIZE,
	    (uint16_t *)host_ip_msg->kvp_ip_val.dns_addr,
	    MAX_IP_ADDR_SIZE,
	    UNUSED_FLAG,
	    &err_dns);

	return (err_ip | err_subnet | err_gway | err_dns | err_adap);
}


/*
 * Prepare a user kvp msg based on host kvp msg (utf16 to utf8)
 * Ensure utf16_utf8 takes care of the additional string terminating char!!
 */
static void
hv_kvp_convert_hostmsg_to_usermsg(struct hv_kvp_msg *hmsg, struct hv_kvp_msg *umsg)
{
	int utf_err = 0;
	uint32_t value_type;
	struct hv_kvp_ip_msg *host_ip_msg;

	host_ip_msg = (struct hv_kvp_ip_msg*)hmsg;
	memset(umsg, 0, sizeof(struct hv_kvp_msg));

	umsg->kvp_hdr.operation = hmsg->kvp_hdr.operation;
	umsg->kvp_hdr.pool = hmsg->kvp_hdr.pool;

	switch (umsg->kvp_hdr.operation) {
	case HV_KVP_OP_SET_IP_INFO:
		hv_kvp_convert_utf16_ipinfo_to_utf8(host_ip_msg, umsg);
		break;

	case HV_KVP_OP_GET_IP_INFO:
		utf16_to_utf8((char *)umsg->body.kvp_ip_val.adapter_id,
		    MAX_ADAPTER_ID_SIZE,
		    (uint16_t *)host_ip_msg->kvp_ip_val.adapter_id,
		    MAX_ADAPTER_ID_SIZE, 1, &utf_err);

		umsg->body.kvp_ip_val.addr_family =
		    host_ip_msg->kvp_ip_val.addr_family;
		break;

	case HV_KVP_OP_SET:
		value_type = hmsg->body.kvp_set.data.value_type;

		switch (value_type) {
		case HV_REG_SZ:
			umsg->body.kvp_set.data.value_size =
			    utf16_to_utf8(
				(char *)umsg->body.kvp_set.data.msg_value.value,
				HV_KVP_EXCHANGE_MAX_VALUE_SIZE - 1,
				(uint16_t *)hmsg->body.kvp_set.data.msg_value.value,
				hmsg->body.kvp_set.data.value_size,
				1, &utf_err);
			/* utf8 encoding */
			umsg->body.kvp_set.data.value_size =
			    umsg->body.kvp_set.data.value_size / 2;
			break;

		case HV_REG_U32:
			umsg->body.kvp_set.data.value_size =
			    sprintf(umsg->body.kvp_set.data.msg_value.value, "%d",
				hmsg->body.kvp_set.data.msg_value.value_u32) + 1;
			break;

		case HV_REG_U64:
			umsg->body.kvp_set.data.value_size =
			    sprintf(umsg->body.kvp_set.data.msg_value.value, "%llu",
				(unsigned long long)
				hmsg->body.kvp_set.data.msg_value.value_u64) + 1;
			break;
		}

		umsg->body.kvp_set.data.key_size =
		    utf16_to_utf8(
			umsg->body.kvp_set.data.key,
			HV_KVP_EXCHANGE_MAX_KEY_SIZE - 1,
			(uint16_t *)hmsg->body.kvp_set.data.key,
			hmsg->body.kvp_set.data.key_size,
			1, &utf_err);

		/* utf8 encoding */
		umsg->body.kvp_set.data.key_size =
		    umsg->body.kvp_set.data.key_size / 2;
		break;

	case HV_KVP_OP_GET:
		umsg->body.kvp_get.data.key_size =
		    utf16_to_utf8(umsg->body.kvp_get.data.key,
			HV_KVP_EXCHANGE_MAX_KEY_SIZE - 1,
			(uint16_t *)hmsg->body.kvp_get.data.key,
			hmsg->body.kvp_get.data.key_size,
			1, &utf_err);
		/* utf8 encoding */
		umsg->body.kvp_get.data.key_size =
		    umsg->body.kvp_get.data.key_size / 2;
		break;

	case HV_KVP_OP_DELETE:
		umsg->body.kvp_delete.key_size =
		    utf16_to_utf8(umsg->body.kvp_delete.key,
			HV_KVP_EXCHANGE_MAX_KEY_SIZE - 1,
			(uint16_t *)hmsg->body.kvp_delete.key,
			hmsg->body.kvp_delete.key_size,
			1, &utf_err);
		/* utf8 encoding */
		umsg->body.kvp_delete.key_size =
		    umsg->body.kvp_delete.key_size / 2;
		break;

	case HV_KVP_OP_ENUMERATE:
		umsg->body.kvp_enum_data.index =
		    hmsg->body.kvp_enum_data.index;
		break;

	default:
		hv_kvp_log_info("%s: daemon_kvp_msg: Invalid operation : %d\n",
		    __func__, umsg->kvp_hdr.operation);
	}
}


/*
 * Prepare a host kvp msg based on user kvp msg (utf8 to utf16)
 */
static int
hv_kvp_convert_usermsg_to_hostmsg(struct hv_kvp_msg *umsg, struct hv_kvp_msg *hmsg)
{
	int hkey_len = 0, hvalue_len = 0, utf_err = 0;
	struct hv_kvp_exchg_msg_value *host_exchg_data;
	char *key_name, *value;

	struct hv_kvp_ip_msg *host_ip_msg = (struct hv_kvp_ip_msg *)hmsg;

	switch (hmsg->kvp_hdr.operation) {
	case HV_KVP_OP_GET_IP_INFO:
		return (hv_kvp_convert_utf8_ipinfo_to_utf16(umsg, host_ip_msg));

	case HV_KVP_OP_SET_IP_INFO:
	case HV_KVP_OP_SET:
	case HV_KVP_OP_DELETE:
		return (0);

	case HV_KVP_OP_ENUMERATE:
		host_exchg_data = &hmsg->body.kvp_enum_data.data;
		key_name = umsg->body.kvp_enum_data.data.key;
		hkey_len = utf8_to_utf16((uint16_t *)host_exchg_data->key,
				((HV_KVP_EXCHANGE_MAX_KEY_SIZE / 2) - 2),
				key_name, strlen(key_name),
				1, &utf_err);
		/* utf16 encoding */
		host_exchg_data->key_size = 2 * (hkey_len + 1);
		value = umsg->body.kvp_enum_data.data.msg_value.value;
		hvalue_len = utf8_to_utf16(
				(uint16_t *)host_exchg_data->msg_value.value,
				((HV_KVP_EXCHANGE_MAX_VALUE_SIZE / 2) - 2),
				value, strlen(value),
				1, &utf_err);
		host_exchg_data->value_size = 2 * (hvalue_len + 1);
		host_exchg_data->value_type = HV_REG_SZ;

		if ((hkey_len < 0) || (hvalue_len < 0))
			return (EINVAL);

		return (0);

	case HV_KVP_OP_GET:
		host_exchg_data = &hmsg->body.kvp_get.data;
		value = umsg->body.kvp_get.data.msg_value.value;
		hvalue_len = utf8_to_utf16(
				(uint16_t *)host_exchg_data->msg_value.value,
				((HV_KVP_EXCHANGE_MAX_VALUE_SIZE / 2) - 2),
				value, strlen(value),
				1, &utf_err);
		/* Convert value size to uft16 */
		host_exchg_data->value_size = 2 * (hvalue_len + 1);
		/* Use values by string */
		host_exchg_data->value_type = HV_REG_SZ;

		if (hvalue_len < 0)
			return (EINVAL);

		return (0);

	default:
		return (EINVAL);
	}
}


/*
 * Send the response back to the host.
 */
static void
hv_kvp_respond_host(hv_kvp_sc *sc, uint32_t error)
{
	struct hv_vmbus_icmsg_hdr *hv_icmsg_hdrp;

	hv_icmsg_hdrp = (struct hv_vmbus_icmsg_hdr *)
	    &sc->rcv_buf[sizeof(struct hv_vmbus_pipe_hdr)];

	hv_icmsg_hdrp->status = error;
	hv_icmsg_hdrp->icflags = HV_ICMSGHDRFLAG_TRANSACTION |
	    HV_ICMSGHDRFLAG_RESPONSE;

	error = vmbus_chan_send(vmbus_get_channel(sc->dev),
	    VMBUS_CHANPKT_TYPE_INBAND, 0, sc->rcv_buf, sc->host_msg_len,
	    sc->host_msg_id);
	if (error)
		hv_kvp_log_info("%s: hv_kvp_respond_host: sendpacket error:%d\n",
			__func__, error);
}


/*
 * This is the main kvp kernel process that interacts with both user daemon
 * and the host
 */
static void
hv_kvp_send_msg_to_daemon(hv_kvp_sc *sc)
{
	struct hv_kvp_msg *hmsg = sc->host_kvp_msg;
	struct hv_kvp_msg *umsg = &sc->daemon_kvp_msg;

	/* Prepare kvp_msg to be sent to user */
	hv_kvp_convert_hostmsg_to_usermsg(hmsg, umsg);

	/* Send the msg to user via function deamon_read - setting sema */
	sema_post(&sc->dev_sema);

	/* We should wake up the daemon, in case it's doing poll() */
	selwakeup(&sc->hv_kvp_selinfo);
}


/*
 * Function to read the kvp request buffer from host
 * and interact with daemon
 */
static void
hv_kvp_process_request(void *context, int pending)
{
	uint8_t *kvp_buf;
	struct vmbus_channel *channel;
	uint32_t recvlen = 0;
	uint64_t requestid;
	struct hv_vmbus_icmsg_hdr *icmsghdrp;
	int ret = 0, error;
	hv_kvp_sc *sc;

	hv_kvp_log_info("%s: entering hv_kvp_process_request\n", __func__);

	sc = (hv_kvp_sc*)context;
	kvp_buf = sc->util_sc.ic_buf;
	channel = vmbus_get_channel(sc->dev);

	recvlen = sc->util_sc.ic_buflen;
	ret = vmbus_chan_recv(channel, kvp_buf, &recvlen, &requestid);
	KASSERT(ret != ENOBUFS, ("hvkvp recvbuf is not large enough"));
	/* XXX check recvlen to make sure that it contains enough data */

	while ((ret == 0) && (recvlen > 0)) {
		icmsghdrp = (struct hv_vmbus_icmsg_hdr *)
		    &kvp_buf[sizeof(struct hv_vmbus_pipe_hdr)];

		hv_kvp_transaction_init(sc, recvlen, requestid, kvp_buf);
		if (icmsghdrp->icmsgtype == HV_ICMSGTYPE_NEGOTIATE) {
			error = vmbus_ic_negomsg(&sc->util_sc,
			    kvp_buf, &recvlen, KVP_FWVER, KVP_MSGVER);
			/* XXX handle vmbus_ic_negomsg failure. */
			if (!error)
				hv_kvp_respond_host(sc, HV_S_OK);
			else
				hv_kvp_respond_host(sc, HV_E_FAIL);
			/*
			 * It is ok to not acquire the mutex before setting
			 * req_in_progress here because negotiation is the
			 * first thing that happens and hence there is no
			 * chance of a race condition.
			 */

			sc->req_in_progress = false;
			hv_kvp_log_info("%s :version negotiated\n", __func__);

		} else {
			if (!sc->daemon_busy) {

				hv_kvp_log_info("%s: issuing qury to daemon\n", __func__);
				mtx_lock(&sc->pending_mutex);
				sc->req_timed_out = false;
				sc->daemon_busy = true;
				mtx_unlock(&sc->pending_mutex);

				hv_kvp_send_msg_to_daemon(sc);
				hv_kvp_log_info("%s: waiting for daemon\n", __func__);
			}

			/* Wait 5 seconds for daemon to respond back */
			tsleep(sc, 0, "kvpworkitem", 5 * hz);
			hv_kvp_log_info("%s: came out of wait\n", __func__);
		}

		mtx_lock(&sc->pending_mutex);

		/* Notice that once req_timed_out is set to true
		 * it will remain true until the next request is
		 * sent to the daemon. The response from daemon
		 * is forwarded to host only when this flag is
		 * false.
		 */
		sc->req_timed_out = true;

		/*
		 * Cancel request if so need be.
		 */
		if (hv_kvp_req_in_progress(sc)) {
			hv_kvp_log_info("%s: request was still active after wait so failing\n", __func__);
			hv_kvp_respond_host(sc, HV_E_FAIL);
			sc->req_in_progress = false;
		}

		mtx_unlock(&sc->pending_mutex);

		/*
		 * Try reading next buffer
		 */
		recvlen = sc->util_sc.ic_buflen;
		ret = vmbus_chan_recv(channel, kvp_buf, &recvlen, &requestid);
		KASSERT(ret != ENOBUFS, ("hvkvp recvbuf is not large enough"));
		/* XXX check recvlen to make sure that it contains enough data */

		hv_kvp_log_info("%s: read: context %p, ret =%d, recvlen=%d\n",
			__func__, context, ret, recvlen);
	}
}


/*
 * Callback routine that gets called whenever there is a message from host
 */
static void
hv_kvp_callback(struct vmbus_channel *chan __unused, void *context)
{
	hv_kvp_sc *sc = (hv_kvp_sc*)context;
	/*
	 The first request from host will not be handled until daemon is registered.
	 when callback is triggered without a registered daemon, callback just return.
	 When a new daemon gets regsitered, this callbcak is trigged from _write op.
	*/
	if (sc->register_done) {
		hv_kvp_log_info("%s: Queuing work item\n", __func__);
		taskqueue_enqueue(taskqueue_thread, &sc->task);
	}
}

static int
hv_kvp_dev_open(struct cdev *dev, int oflags, int devtype,
				struct thread *td)
{
	hv_kvp_sc *sc = (hv_kvp_sc*)dev->si_drv1;

	hv_kvp_log_info("%s: Opened device \"hv_kvp_device\" successfully.\n", __func__);
	if (sc->dev_accessed)
		return (-EBUSY);

	sc->daemon_task = curproc;
	sc->dev_accessed = true;
	sc->daemon_busy = false;
	return (0);
}


static int
hv_kvp_dev_close(struct cdev *dev __unused, int fflag __unused, int devtype __unused,
				 struct thread *td __unused)
{
	hv_kvp_sc *sc = (hv_kvp_sc*)dev->si_drv1;

	hv_kvp_log_info("%s: Closing device \"hv_kvp_device\".\n", __func__);
	sc->dev_accessed = false;
	sc->register_done = false;
	return (0);
}


/*
 * hv_kvp_daemon read invokes this function
 * acts as a send to daemon
 */
static int
hv_kvp_dev_daemon_read(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
	size_t amt;
	int error = 0;
	struct hv_kvp_msg *hv_kvp_dev_buf;
	hv_kvp_sc *sc = (hv_kvp_sc*)dev->si_drv1;

	/* Read is not allowed util registering is done. */
	if (!sc->register_done)
		return (EPERM);

	sema_wait(&sc->dev_sema);

	hv_kvp_dev_buf = malloc(sizeof(*hv_kvp_dev_buf), M_TEMP, M_WAITOK);
	memcpy(hv_kvp_dev_buf, &sc->daemon_kvp_msg, sizeof(struct hv_kvp_msg));

	amt = MIN(uio->uio_resid, uio->uio_offset >= BUFFERSIZE + 1 ? 0 :
		BUFFERSIZE + 1 - uio->uio_offset);

	if ((error = uiomove(hv_kvp_dev_buf, amt, uio)) != 0)
		hv_kvp_log_info("%s: hv_kvp uiomove read failed!\n", __func__);

	free(hv_kvp_dev_buf, M_TEMP);
	return (error);
}


/*
 * hv_kvp_daemon write invokes this function
 * acts as a receive from daemon
 */
static int
hv_kvp_dev_daemon_write(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
	size_t amt;
	int error = 0;
	struct hv_kvp_msg *hv_kvp_dev_buf;
	hv_kvp_sc *sc = (hv_kvp_sc*)dev->si_drv1;

	uio->uio_offset = 0;
	hv_kvp_dev_buf = malloc(sizeof(*hv_kvp_dev_buf), M_TEMP, M_WAITOK);

	amt = MIN(uio->uio_resid, BUFFERSIZE);
	error = uiomove(hv_kvp_dev_buf, amt, uio);

	if (error != 0) {
		free(hv_kvp_dev_buf, M_TEMP);
		return (error);
	}
	memcpy(&sc->daemon_kvp_msg, hv_kvp_dev_buf, sizeof(struct hv_kvp_msg));

	free(hv_kvp_dev_buf, M_TEMP);
	if (sc->register_done == false) {
		if (sc->daemon_kvp_msg.kvp_hdr.operation == HV_KVP_OP_REGISTER) {
			sc->register_done = true;
			hv_kvp_callback(vmbus_get_channel(sc->dev), dev->si_drv1);
		}
		else {
			hv_kvp_log_info("%s, KVP Registration Failed\n", __func__);
			return (EINVAL);
		}
	} else {

		mtx_lock(&sc->pending_mutex);

		if(!sc->req_timed_out) {
			struct hv_kvp_msg *hmsg = sc->host_kvp_msg;
			struct hv_kvp_msg *umsg = &sc->daemon_kvp_msg;

			error = hv_kvp_convert_usermsg_to_hostmsg(umsg, hmsg);
			hv_kvp_respond_host(sc, umsg->hdr.error);
			wakeup(sc);
			sc->req_in_progress = false;
			if (umsg->hdr.error != HV_S_OK)
				hv_kvp_log_info("%s, Error 0x%x from daemon\n",
				    __func__, umsg->hdr.error);
			if (error)
				hv_kvp_log_info("%s, Error from convert\n", __func__);
		}

		sc->daemon_busy = false;
		mtx_unlock(&sc->pending_mutex);
	}

	return (error);
}


/*
 * hv_kvp_daemon poll invokes this function to check if data is available
 * for daemon to read.
 */
static int
hv_kvp_dev_daemon_poll(struct cdev *dev, int events, struct thread *td)
{
	int revents = 0;
	hv_kvp_sc *sc = (hv_kvp_sc*)dev->si_drv1;

	mtx_lock(&sc->pending_mutex);
	/*
	 * We check global flag daemon_busy for the data availiability for
	 * userland to read. Deamon_busy is set to true before driver has data
	 * for daemon to read. It is set to false after daemon sends
	 * then response back to driver.
	 */
	if (sc->daemon_busy == true)
		revents = POLLIN;
	else
		selrecord(td, &sc->hv_kvp_selinfo);

	mtx_unlock(&sc->pending_mutex);

	return (revents);
}

static int
hv_kvp_probe(device_t dev)
{

	return (vmbus_ic_probe(dev, vmbus_kvp_descs));
}

static int
hv_kvp_attach(device_t dev)
{
	int error;
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx;

	hv_kvp_sc *sc = (hv_kvp_sc*)device_get_softc(dev);

	sc->dev = dev;
	sema_init(&sc->dev_sema, 0, "hv_kvp device semaphore");
	mtx_init(&sc->pending_mutex, "hv-kvp pending mutex",
		NULL, MTX_DEF);

	ctx = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "hv_kvp_log",
	    CTLFLAG_RWTUN, &hv_kvp_log, 0, "Hyperv KVP service log level");

	TASK_INIT(&sc->task, 0, hv_kvp_process_request, sc);

	/* create character device */
	error = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
			&sc->hv_kvp_dev,
			&hv_kvp_cdevsw,
			0,
			UID_ROOT,
			GID_WHEEL,
			0640,
			"hv_kvp_dev");

	if (error != 0)
		return (error);
	sc->hv_kvp_dev->si_drv1 = sc;

	return (vmbus_ic_attach(dev, hv_kvp_callback));
}

static int
hv_kvp_detach(device_t dev)
{
	hv_kvp_sc *sc = (hv_kvp_sc*)device_get_softc(dev);

	if (sc->daemon_task != NULL) {
		PROC_LOCK(sc->daemon_task);
		kern_psignal(sc->daemon_task, SIGKILL);
		PROC_UNLOCK(sc->daemon_task);
	}

	destroy_dev(sc->hv_kvp_dev);
	return (vmbus_ic_detach(dev));
}

static device_method_t kvp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, hv_kvp_probe),
	DEVMETHOD(device_attach, hv_kvp_attach),
	DEVMETHOD(device_detach, hv_kvp_detach),
	{ 0, 0 }
};

static driver_t kvp_driver = { "hvkvp", kvp_methods, sizeof(hv_kvp_sc)};

static devclass_t kvp_devclass;

DRIVER_MODULE(hv_kvp, vmbus, kvp_driver, kvp_devclass, NULL, NULL);
MODULE_VERSION(hv_kvp, 1);
MODULE_DEPEND(hv_kvp, vmbus, 1, 1, 1);
