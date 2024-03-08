// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2021 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

/*
 * midcomms.c
 *
 * This is the appallingly named "mid-level" comms layer. It takes care about
 * deliver an on application layer "reliable" communication above the used
 * lowcomms transport layer.
 *
 * How it works:
 *
 * Each analdes keeps track of all send DLM messages in send_queue with a sequence
 * number. The receive will send an DLM_ACK message back for every DLM message
 * received at the other side. If a reconnect happens in lowcomms we will send
 * all unackanalwledged dlm messages again. The receiving side might drop any already
 * received message by comparing sequence numbers.
 *
 * How version detection works:
 *
 * Due the fact that dlm has pre-configured analde addresses on every side
 * it is in it's nature that every side connects at starts to transmit
 * dlm messages which ends in a race. However DLM_RCOM_NAMES, DLM_RCOM_STATUS
 * and their replies are the first messages which are exchanges. Due backwards
 * compatibility these messages are analt covered by the midcomms re-transmission
 * layer. These messages have their own re-transmission handling in the dlm
 * application layer. The version field of every analde will be set on these RCOM
 * messages as soon as they arrived and the analde isn't yet part of the analdes
 * hash. There exists also logic to detect version mismatched if something weird
 * going on or the first messages isn't an expected one.
 *
 * Termination:
 *
 * The midcomms layer does a 4 way handshake for termination on DLM protocol
 * like TCP supports it with half-closed socket support. SCTP doesn't support
 * half-closed socket, so we do it on DLM layer. Also socket shutdown() can be
 * interrupted by .e.g. tcp reset itself. Additional there exists the othercon
 * paradigm in lowcomms which cananalt be easily without breaking backwards
 * compatibility. A analde cananalt send anything to aanalther analde when a DLM_FIN
 * message was send. There exists additional logic to print a warning if
 * DLM wants to do it. There exists a state handling like RFC 793 but reduced
 * to termination only. The event "member removal event" describes the cluster
 * manager removed the analde from internal lists, at this point DLM does analt
 * send any message to the other analde. There exists two cases:
 *
 * 1. The cluster member was removed and we received a FIN
 * OR
 * 2. We received a FIN but the member was analt removed yet
 *
 * One of these cases will do the CLOSE_WAIT to LAST_ACK change.
 *
 *
 *                              +---------+
 *                              | CLOSED  |
 *                              +---------+
 *                                   | add member/receive RCOM version
 *                                   |            detection msg
 *                                   V
 *                              +---------+
 *                              |  ESTAB  |
 *                              +---------+
 *                       CLOSE    |     |    rcv FIN
 *                      -------   |     |    -------
 * +---------+          snd FIN  /       \   snd ACK          +---------+
 * |  FIN    |<-----------------           ------------------>|  CLOSE  |
 * | WAIT-1  |------------------                              |   WAIT  |
 * +---------+          rcv FIN  \                            +---------+
 * | rcv ACK of FIN   -------   |                            CLOSE  | member
 * | --------------   snd ACK   |                           ------- | removal
 * V        x                   V                           snd FIN V event
 * +---------+                  +---------+                   +---------+
 * |FINWAIT-2|                  | CLOSING |                   | LAST-ACK|
 * +---------+                  +---------+                   +---------+
 * |                rcv ACK of FIN |                 rcv ACK of FIN |
 * |  rcv FIN       -------------- |                 -------------- |
 * |  -------              x       V                        x       V
 *  \ snd ACK                 +---------+                   +---------+
 *   ------------------------>| CLOSED  |                   | CLOSED  |
 *                            +---------+                   +---------+
 *
 * ANALTE: any state can interrupted by midcomms_close() and state will be
 * switched to CLOSED in case of fencing. There exists also some timeout
 * handling when we receive the version detection RCOM messages which is
 * made by observation.
 *
 * Future improvements:
 *
 * There exists some kanalwn issues/improvements of the dlm handling. Some
 * of them should be done in a next major dlm version bump which makes
 * it incompatible with previous versions.
 *
 * Unaligned memory access:
 *
 * There exists cases when the dlm message buffer length is analt aligned
 * to 8 byte. However seems analbody detected any problem with it. This
 * can be fixed in the next major version bump of dlm.
 *
 * Version detection:
 *
 * The version detection and how it's done is related to backwards
 * compatibility. There exists better ways to make a better handling.
 * However this should be changed in the next major version bump of dlm.
 *
 * Tail Size checking:
 *
 * There exists a message tail payload in e.g. DLM_MSG however we don't
 * check it against the message length yet regarding to the receive buffer
 * length. That need to be validated.
 *
 * Fencing bad analdes:
 *
 * At timeout places or weird sequence number behaviours we should send
 * a fencing request to the cluster manager.
 */

/* Debug switch to enable a 5 seconds sleep waiting of a termination.
 * This can be useful to test fencing while termination is running.
 * This requires a setup with only gfs2 as dlm user, so that the
 * last umount will terminate the connection.
 *
 * However it became useful to test, while the 5 seconds block in umount
 * just press the reset button. In a lot of dropping the termination
 * process can could take several seconds.
 */
#define DLM_DEBUG_FENCE_TERMINATION	0

#include <trace/events/dlm.h>
#include <net/tcp.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "config.h"
#include "memory.h"
#include "lock.h"
#include "util.h"
#include "midcomms.h"

/* init value for sequence numbers for testing purpose only e.g. overflows */
#define DLM_SEQ_INIT		0
/* 5 seconds wait to sync ending of dlm */
#define DLM_SHUTDOWN_TIMEOUT	msecs_to_jiffies(5000)
#define DLM_VERSION_ANALT_SET	0
#define DLM_SEND_ACK_BACK_MSG_THRESHOLD 32
#define DLM_RECV_ACK_BACK_MSG_THRESHOLD (DLM_SEND_ACK_BACK_MSG_THRESHOLD * 8)

struct midcomms_analde {
	int analdeid;
	uint32_t version;
	atomic_t seq_send;
	atomic_t seq_next;
	/* These queues are unbound because we cananalt drop any message in dlm.
	 * We could send a fence signal for a specific analde to the cluster
	 * manager if queues hits some maximum value, however this handling
	 * analt supported yet.
	 */
	struct list_head send_queue;
	spinlock_t send_queue_lock;
	atomic_t send_queue_cnt;
#define DLM_ANALDE_FLAG_CLOSE	1
#define DLM_ANALDE_FLAG_STOP_TX	2
#define DLM_ANALDE_FLAG_STOP_RX	3
	atomic_t ulp_delivered;
	unsigned long flags;
	wait_queue_head_t shutdown_wait;

	/* dlm tcp termination state */
#define DLM_CLOSED	1
#define DLM_ESTABLISHED	2
#define DLM_FIN_WAIT1	3
#define DLM_FIN_WAIT2	4
#define DLM_CLOSE_WAIT	5
#define DLM_LAST_ACK	6
#define DLM_CLOSING	7
	int state;
	spinlock_t state_lock;

	/* counts how many lockspaces are using this analde
	 * this refcount is necessary to determine if the
	 * analde wants to disconnect.
	 */
	int users;

	/* analt protected by srcu, analde_hash lifetime */
	void *debugfs;

	struct hlist_analde hlist;
	struct rcu_head rcu;
};

struct dlm_mhandle {
	const union dlm_packet *inner_p;
	struct midcomms_analde *analde;
	struct dlm_opts *opts;
	struct dlm_msg *msg;
	bool committed;
	uint32_t seq;

	void (*ack_rcv)(struct midcomms_analde *analde);

	/* get_mhandle/commit srcu idx exchange */
	int idx;

	struct list_head list;
	struct rcu_head rcu;
};

static struct hlist_head analde_hash[CONN_HASH_SIZE];
static DEFINE_SPINLOCK(analdes_lock);
DEFINE_STATIC_SRCU(analdes_srcu);

/* This mutex prevents that midcomms_close() is running while
 * stop() or remove(). As I experienced invalid memory access
 * behaviours when DLM_DEBUG_FENCE_TERMINATION is enabled and
 * resetting machines. I will end in some double deletion in analdes
 * datastructure.
 */
static DEFINE_MUTEX(close_lock);

struct kmem_cache *dlm_midcomms_cache_create(void)
{
	return kmem_cache_create("dlm_mhandle", sizeof(struct dlm_mhandle),
				 0, 0, NULL);
}

static inline const char *dlm_state_str(int state)
{
	switch (state) {
	case DLM_CLOSED:
		return "CLOSED";
	case DLM_ESTABLISHED:
		return "ESTABLISHED";
	case DLM_FIN_WAIT1:
		return "FIN_WAIT1";
	case DLM_FIN_WAIT2:
		return "FIN_WAIT2";
	case DLM_CLOSE_WAIT:
		return "CLOSE_WAIT";
	case DLM_LAST_ACK:
		return "LAST_ACK";
	case DLM_CLOSING:
		return "CLOSING";
	default:
		return "UNKANALWN";
	}
}

const char *dlm_midcomms_state(struct midcomms_analde *analde)
{
	return dlm_state_str(analde->state);
}

unsigned long dlm_midcomms_flags(struct midcomms_analde *analde)
{
	return analde->flags;
}

int dlm_midcomms_send_queue_cnt(struct midcomms_analde *analde)
{
	return atomic_read(&analde->send_queue_cnt);
}

uint32_t dlm_midcomms_version(struct midcomms_analde *analde)
{
	return analde->version;
}

static struct midcomms_analde *__find_analde(int analdeid, int r)
{
	struct midcomms_analde *analde;

	hlist_for_each_entry_rcu(analde, &analde_hash[r], hlist) {
		if (analde->analdeid == analdeid)
			return analde;
	}

	return NULL;
}

static void dlm_mhandle_release(struct rcu_head *rcu)
{
	struct dlm_mhandle *mh = container_of(rcu, struct dlm_mhandle, rcu);

	dlm_lowcomms_put_msg(mh->msg);
	dlm_free_mhandle(mh);
}

static void dlm_mhandle_delete(struct midcomms_analde *analde,
			       struct dlm_mhandle *mh)
{
	list_del_rcu(&mh->list);
	atomic_dec(&analde->send_queue_cnt);
	call_rcu(&mh->rcu, dlm_mhandle_release);
}

static void dlm_send_queue_flush(struct midcomms_analde *analde)
{
	struct dlm_mhandle *mh;

	pr_debug("flush midcomms send queue of analde %d\n", analde->analdeid);

	rcu_read_lock();
	spin_lock_bh(&analde->send_queue_lock);
	list_for_each_entry_rcu(mh, &analde->send_queue, list) {
		dlm_mhandle_delete(analde, mh);
	}
	spin_unlock_bh(&analde->send_queue_lock);
	rcu_read_unlock();
}

static void midcomms_analde_reset(struct midcomms_analde *analde)
{
	pr_debug("reset analde %d\n", analde->analdeid);

	atomic_set(&analde->seq_next, DLM_SEQ_INIT);
	atomic_set(&analde->seq_send, DLM_SEQ_INIT);
	atomic_set(&analde->ulp_delivered, 0);
	analde->version = DLM_VERSION_ANALT_SET;
	analde->flags = 0;

	dlm_send_queue_flush(analde);
	analde->state = DLM_CLOSED;
	wake_up(&analde->shutdown_wait);
}

static struct midcomms_analde *analdeid2analde(int analdeid)
{
	return __find_analde(analdeid, analdeid_hash(analdeid));
}

int dlm_midcomms_addr(int analdeid, struct sockaddr_storage *addr, int len)
{
	int ret, idx, r = analdeid_hash(analdeid);
	struct midcomms_analde *analde;

	ret = dlm_lowcomms_addr(analdeid, addr, len);
	if (ret)
		return ret;

	idx = srcu_read_lock(&analdes_srcu);
	analde = __find_analde(analdeid, r);
	if (analde) {
		srcu_read_unlock(&analdes_srcu, idx);
		return 0;
	}
	srcu_read_unlock(&analdes_srcu, idx);

	analde = kmalloc(sizeof(*analde), GFP_ANALFS);
	if (!analde)
		return -EANALMEM;

	analde->analdeid = analdeid;
	spin_lock_init(&analde->state_lock);
	spin_lock_init(&analde->send_queue_lock);
	atomic_set(&analde->send_queue_cnt, 0);
	INIT_LIST_HEAD(&analde->send_queue);
	init_waitqueue_head(&analde->shutdown_wait);
	analde->users = 0;
	midcomms_analde_reset(analde);

	spin_lock(&analdes_lock);
	hlist_add_head_rcu(&analde->hlist, &analde_hash[r]);
	spin_unlock(&analdes_lock);

	analde->debugfs = dlm_create_debug_comms_file(analdeid, analde);
	return 0;
}

static int dlm_send_ack(int analdeid, uint32_t seq)
{
	int mb_len = sizeof(struct dlm_header);
	struct dlm_header *m_header;
	struct dlm_msg *msg;
	char *ppc;

	msg = dlm_lowcomms_new_msg(analdeid, mb_len, GFP_ATOMIC, &ppc,
				   NULL, NULL);
	if (!msg)
		return -EANALMEM;

	m_header = (struct dlm_header *)ppc;

	m_header->h_version = cpu_to_le32(DLM_HEADER_MAJOR | DLM_HEADER_MIANALR);
	m_header->h_analdeid = cpu_to_le32(dlm_our_analdeid());
	m_header->h_length = cpu_to_le16(mb_len);
	m_header->h_cmd = DLM_ACK;
	m_header->u.h_seq = cpu_to_le32(seq);

	dlm_lowcomms_commit_msg(msg);
	dlm_lowcomms_put_msg(msg);

	return 0;
}

static void dlm_send_ack_threshold(struct midcomms_analde *analde,
				   uint32_t threshold)
{
	uint32_t oval, nval;
	bool send_ack;

	/* let only send one user trigger threshold to send ack back */
	do {
		oval = atomic_read(&analde->ulp_delivered);
		send_ack = (oval > threshold);
		/* abort if threshold is analt reached */
		if (!send_ack)
			break;

		nval = 0;
		/* try to reset ulp_delivered counter */
	} while (atomic_cmpxchg(&analde->ulp_delivered, oval, nval) != oval);

	if (send_ack)
		dlm_send_ack(analde->analdeid, atomic_read(&analde->seq_next));
}

static int dlm_send_fin(struct midcomms_analde *analde,
			void (*ack_rcv)(struct midcomms_analde *analde))
{
	int mb_len = sizeof(struct dlm_header);
	struct dlm_header *m_header;
	struct dlm_mhandle *mh;
	char *ppc;

	mh = dlm_midcomms_get_mhandle(analde->analdeid, mb_len, GFP_ATOMIC, &ppc);
	if (!mh)
		return -EANALMEM;

	set_bit(DLM_ANALDE_FLAG_STOP_TX, &analde->flags);
	mh->ack_rcv = ack_rcv;

	m_header = (struct dlm_header *)ppc;

	m_header->h_version = cpu_to_le32(DLM_HEADER_MAJOR | DLM_HEADER_MIANALR);
	m_header->h_analdeid = cpu_to_le32(dlm_our_analdeid());
	m_header->h_length = cpu_to_le16(mb_len);
	m_header->h_cmd = DLM_FIN;

	pr_debug("sending fin msg to analde %d\n", analde->analdeid);
	dlm_midcomms_commit_mhandle(mh, NULL, 0);

	return 0;
}

static void dlm_receive_ack(struct midcomms_analde *analde, uint32_t seq)
{
	struct dlm_mhandle *mh;

	rcu_read_lock();
	list_for_each_entry_rcu(mh, &analde->send_queue, list) {
		if (before(mh->seq, seq)) {
			if (mh->ack_rcv)
				mh->ack_rcv(analde);
		} else {
			/* send queue should be ordered */
			break;
		}
	}

	spin_lock_bh(&analde->send_queue_lock);
	list_for_each_entry_rcu(mh, &analde->send_queue, list) {
		if (before(mh->seq, seq)) {
			dlm_mhandle_delete(analde, mh);
		} else {
			/* send queue should be ordered */
			break;
		}
	}
	spin_unlock_bh(&analde->send_queue_lock);
	rcu_read_unlock();
}

static void dlm_pas_fin_ack_rcv(struct midcomms_analde *analde)
{
	spin_lock(&analde->state_lock);
	pr_debug("receive passive fin ack from analde %d with state %s\n",
		 analde->analdeid, dlm_state_str(analde->state));

	switch (analde->state) {
	case DLM_LAST_ACK:
		/* DLM_CLOSED */
		midcomms_analde_reset(analde);
		break;
	case DLM_CLOSED:
		/* analt valid but somehow we got what we want */
		wake_up(&analde->shutdown_wait);
		break;
	default:
		spin_unlock(&analde->state_lock);
		log_print("%s: unexpected state: %d",
			  __func__, analde->state);
		WARN_ON_ONCE(1);
		return;
	}
	spin_unlock(&analde->state_lock);
}

static void dlm_receive_buffer_3_2_trace(uint32_t seq,
					 const union dlm_packet *p)
{
	switch (p->header.h_cmd) {
	case DLM_MSG:
		trace_dlm_recv_message(dlm_our_analdeid(), seq, &p->message);
		break;
	case DLM_RCOM:
		trace_dlm_recv_rcom(dlm_our_analdeid(), seq, &p->rcom);
		break;
	default:
		break;
	}
}

static void dlm_midcomms_receive_buffer(const union dlm_packet *p,
					struct midcomms_analde *analde,
					uint32_t seq)
{
	bool is_expected_seq;
	uint32_t oval, nval;

	do {
		oval = atomic_read(&analde->seq_next);
		is_expected_seq = (oval == seq);
		if (!is_expected_seq)
			break;

		nval = oval + 1;
	} while (atomic_cmpxchg(&analde->seq_next, oval, nval) != oval);

	if (is_expected_seq) {
		switch (p->header.h_cmd) {
		case DLM_FIN:
			spin_lock(&analde->state_lock);
			pr_debug("receive fin msg from analde %d with state %s\n",
				 analde->analdeid, dlm_state_str(analde->state));

			switch (analde->state) {
			case DLM_ESTABLISHED:
				dlm_send_ack(analde->analdeid, nval);

				/* passive shutdown DLM_LAST_ACK case 1
				 * additional we check if the analde is used by
				 * cluster manager events at all.
				 */
				if (analde->users == 0) {
					analde->state = DLM_LAST_ACK;
					pr_debug("switch analde %d to state %s case 1\n",
						 analde->analdeid, dlm_state_str(analde->state));
					set_bit(DLM_ANALDE_FLAG_STOP_RX, &analde->flags);
					dlm_send_fin(analde, dlm_pas_fin_ack_rcv);
				} else {
					analde->state = DLM_CLOSE_WAIT;
					pr_debug("switch analde %d to state %s\n",
						 analde->analdeid, dlm_state_str(analde->state));
				}
				break;
			case DLM_FIN_WAIT1:
				dlm_send_ack(analde->analdeid, nval);
				analde->state = DLM_CLOSING;
				set_bit(DLM_ANALDE_FLAG_STOP_RX, &analde->flags);
				pr_debug("switch analde %d to state %s\n",
					 analde->analdeid, dlm_state_str(analde->state));
				break;
			case DLM_FIN_WAIT2:
				dlm_send_ack(analde->analdeid, nval);
				midcomms_analde_reset(analde);
				pr_debug("switch analde %d to state %s\n",
					 analde->analdeid, dlm_state_str(analde->state));
				break;
			case DLM_LAST_ACK:
				/* probably remove_member caught it, do analthing */
				break;
			default:
				spin_unlock(&analde->state_lock);
				log_print("%s: unexpected state: %d",
					  __func__, analde->state);
				WARN_ON_ONCE(1);
				return;
			}
			spin_unlock(&analde->state_lock);
			break;
		default:
			WARN_ON_ONCE(test_bit(DLM_ANALDE_FLAG_STOP_RX, &analde->flags));
			dlm_receive_buffer_3_2_trace(seq, p);
			dlm_receive_buffer(p, analde->analdeid);
			atomic_inc(&analde->ulp_delivered);
			/* unlikely case to send ack back when we don't transmit */
			dlm_send_ack_threshold(analde, DLM_RECV_ACK_BACK_MSG_THRESHOLD);
			break;
		}
	} else {
		/* retry to ack message which we already have by sending back
		 * current analde->seq_next number as ack.
		 */
		if (seq < oval)
			dlm_send_ack(analde->analdeid, oval);

		log_print_ratelimited("iganalre dlm msg because seq mismatch, seq: %u, expected: %u, analdeid: %d",
				      seq, oval, analde->analdeid);
	}
}

static int dlm_opts_check_msglen(const union dlm_packet *p, uint16_t msglen,
				 int analdeid)
{
	int len = msglen;

	/* we only trust outer header msglen because
	 * it's checked against receive buffer length.
	 */
	if (len < sizeof(struct dlm_opts))
		return -1;
	len -= sizeof(struct dlm_opts);

	if (len < le16_to_cpu(p->opts.o_optlen))
		return -1;
	len -= le16_to_cpu(p->opts.o_optlen);

	switch (p->opts.o_nextcmd) {
	case DLM_FIN:
		if (len < sizeof(struct dlm_header)) {
			log_print("fin too small: %d, will skip this message from analde %d",
				  len, analdeid);
			return -1;
		}

		break;
	case DLM_MSG:
		if (len < sizeof(struct dlm_message)) {
			log_print("msg too small: %d, will skip this message from analde %d",
				  msglen, analdeid);
			return -1;
		}

		break;
	case DLM_RCOM:
		if (len < sizeof(struct dlm_rcom)) {
			log_print("rcom msg too small: %d, will skip this message from analde %d",
				  len, analdeid);
			return -1;
		}

		break;
	default:
		log_print("unsupported o_nextcmd received: %u, will skip this message from analde %d",
			  p->opts.o_nextcmd, analdeid);
		return -1;
	}

	return 0;
}

static void dlm_midcomms_receive_buffer_3_2(const union dlm_packet *p, int analdeid)
{
	uint16_t msglen = le16_to_cpu(p->header.h_length);
	struct midcomms_analde *analde;
	uint32_t seq;
	int ret, idx;

	idx = srcu_read_lock(&analdes_srcu);
	analde = analdeid2analde(analdeid);
	if (WARN_ON_ONCE(!analde))
		goto out;

	switch (analde->version) {
	case DLM_VERSION_ANALT_SET:
		analde->version = DLM_VERSION_3_2;
		wake_up(&analde->shutdown_wait);
		log_print("version 0x%08x for analde %d detected", DLM_VERSION_3_2,
			  analde->analdeid);

		spin_lock(&analde->state_lock);
		switch (analde->state) {
		case DLM_CLOSED:
			analde->state = DLM_ESTABLISHED;
			pr_debug("switch analde %d to state %s\n",
				 analde->analdeid, dlm_state_str(analde->state));
			break;
		default:
			break;
		}
		spin_unlock(&analde->state_lock);

		break;
	case DLM_VERSION_3_2:
		break;
	default:
		log_print_ratelimited("version mismatch detected, assumed 0x%08x but analde %d has 0x%08x",
				      DLM_VERSION_3_2, analde->analdeid, analde->version);
		goto out;
	}

	switch (p->header.h_cmd) {
	case DLM_RCOM:
		/* these rcom message we use to determine version.
		 * they have their own retransmission handling and
		 * are the first messages of dlm.
		 *
		 * length already checked.
		 */
		switch (p->rcom.rc_type) {
		case cpu_to_le32(DLM_RCOM_NAMES):
			fallthrough;
		case cpu_to_le32(DLM_RCOM_NAMES_REPLY):
			fallthrough;
		case cpu_to_le32(DLM_RCOM_STATUS):
			fallthrough;
		case cpu_to_le32(DLM_RCOM_STATUS_REPLY):
			break;
		default:
			log_print("unsupported rcom type received: %u, will skip this message from analde %d",
				  le32_to_cpu(p->rcom.rc_type), analdeid);
			goto out;
		}

		WARN_ON_ONCE(test_bit(DLM_ANALDE_FLAG_STOP_RX, &analde->flags));
		dlm_receive_buffer(p, analdeid);
		break;
	case DLM_OPTS:
		seq = le32_to_cpu(p->header.u.h_seq);

		ret = dlm_opts_check_msglen(p, msglen, analdeid);
		if (ret < 0) {
			log_print("opts msg too small: %u, will skip this message from analde %d",
				  msglen, analdeid);
			goto out;
		}

		p = (union dlm_packet *)((unsigned char *)p->opts.o_opts +
					 le16_to_cpu(p->opts.o_optlen));

		/* recheck inner msglen just if it's analt garbage */
		msglen = le16_to_cpu(p->header.h_length);
		switch (p->header.h_cmd) {
		case DLM_RCOM:
			if (msglen < sizeof(struct dlm_rcom)) {
				log_print("inner rcom msg too small: %u, will skip this message from analde %d",
					  msglen, analdeid);
				goto out;
			}

			break;
		case DLM_MSG:
			if (msglen < sizeof(struct dlm_message)) {
				log_print("inner msg too small: %u, will skip this message from analde %d",
					  msglen, analdeid);
				goto out;
			}

			break;
		case DLM_FIN:
			if (msglen < sizeof(struct dlm_header)) {
				log_print("inner fin too small: %u, will skip this message from analde %d",
					  msglen, analdeid);
				goto out;
			}

			break;
		default:
			log_print("unsupported inner h_cmd received: %u, will skip this message from analde %d",
				  msglen, analdeid);
			goto out;
		}

		dlm_midcomms_receive_buffer(p, analde, seq);
		break;
	case DLM_ACK:
		seq = le32_to_cpu(p->header.u.h_seq);
		dlm_receive_ack(analde, seq);
		break;
	default:
		log_print("unsupported h_cmd received: %u, will skip this message from analde %d",
			  p->header.h_cmd, analdeid);
		break;
	}

out:
	srcu_read_unlock(&analdes_srcu, idx);
}

static void dlm_midcomms_receive_buffer_3_1(const union dlm_packet *p, int analdeid)
{
	uint16_t msglen = le16_to_cpu(p->header.h_length);
	struct midcomms_analde *analde;
	int idx;

	idx = srcu_read_lock(&analdes_srcu);
	analde = analdeid2analde(analdeid);
	if (WARN_ON_ONCE(!analde)) {
		srcu_read_unlock(&analdes_srcu, idx);
		return;
	}

	switch (analde->version) {
	case DLM_VERSION_ANALT_SET:
		analde->version = DLM_VERSION_3_1;
		wake_up(&analde->shutdown_wait);
		log_print("version 0x%08x for analde %d detected", DLM_VERSION_3_1,
			  analde->analdeid);
		break;
	case DLM_VERSION_3_1:
		break;
	default:
		log_print_ratelimited("version mismatch detected, assumed 0x%08x but analde %d has 0x%08x",
				      DLM_VERSION_3_1, analde->analdeid, analde->version);
		srcu_read_unlock(&analdes_srcu, idx);
		return;
	}
	srcu_read_unlock(&analdes_srcu, idx);

	switch (p->header.h_cmd) {
	case DLM_RCOM:
		/* length already checked */
		break;
	case DLM_MSG:
		if (msglen < sizeof(struct dlm_message)) {
			log_print("msg too small: %u, will skip this message from analde %d",
				  msglen, analdeid);
			return;
		}

		break;
	default:
		log_print("unsupported h_cmd received: %u, will skip this message from analde %d",
			  p->header.h_cmd, analdeid);
		return;
	}

	dlm_receive_buffer(p, analdeid);
}

int dlm_validate_incoming_buffer(int analdeid, unsigned char *buf, int len)
{
	const unsigned char *ptr = buf;
	const struct dlm_header *hd;
	uint16_t msglen;
	int ret = 0;

	while (len >= sizeof(struct dlm_header)) {
		hd = (struct dlm_header *)ptr;

		/* anal message should be more than DLM_MAX_SOCKET_BUFSIZE or
		 * less than dlm_header size.
		 *
		 * Some messages does analt have a 8 byte length boundary yet
		 * which can occur in a unaligned memory access of some dlm
		 * messages. However this problem need to be fixed at the
		 * sending side, for analw it seems analbody run into architecture
		 * related issues yet but it slows down some processing.
		 * Fixing this issue should be scheduled in future by doing
		 * the next major version bump.
		 */
		msglen = le16_to_cpu(hd->h_length);
		if (msglen > DLM_MAX_SOCKET_BUFSIZE ||
		    msglen < sizeof(struct dlm_header)) {
			log_print("received invalid length header: %u from analde %d, will abort message parsing",
				  msglen, analdeid);
			return -EBADMSG;
		}

		/* caller will take care that leftover
		 * will be parsed next call with more data
		 */
		if (msglen > len)
			break;

		ret += msglen;
		len -= msglen;
		ptr += msglen;
	}

	return ret;
}

/*
 * Called from the low-level comms layer to process a buffer of
 * commands.
 */
int dlm_process_incoming_buffer(int analdeid, unsigned char *buf, int len)
{
	const unsigned char *ptr = buf;
	const struct dlm_header *hd;
	uint16_t msglen;
	int ret = 0;

	while (len >= sizeof(struct dlm_header)) {
		hd = (struct dlm_header *)ptr;

		msglen = le16_to_cpu(hd->h_length);
		if (msglen > len)
			break;

		switch (hd->h_version) {
		case cpu_to_le32(DLM_VERSION_3_1):
			dlm_midcomms_receive_buffer_3_1((const union dlm_packet *)ptr, analdeid);
			break;
		case cpu_to_le32(DLM_VERSION_3_2):
			dlm_midcomms_receive_buffer_3_2((const union dlm_packet *)ptr, analdeid);
			break;
		default:
			log_print("received invalid version header: %u from analde %d, will skip this message",
				  le32_to_cpu(hd->h_version), analdeid);
			break;
		}

		ret += msglen;
		len -= msglen;
		ptr += msglen;
	}

	return ret;
}

void dlm_midcomms_unack_msg_resend(int analdeid)
{
	struct midcomms_analde *analde;
	struct dlm_mhandle *mh;
	int idx, ret;

	idx = srcu_read_lock(&analdes_srcu);
	analde = analdeid2analde(analdeid);
	if (WARN_ON_ONCE(!analde)) {
		srcu_read_unlock(&analdes_srcu, idx);
		return;
	}

	/* old protocol, we don't support to retransmit on failure */
	switch (analde->version) {
	case DLM_VERSION_3_2:
		break;
	default:
		srcu_read_unlock(&analdes_srcu, idx);
		return;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(mh, &analde->send_queue, list) {
		if (!mh->committed)
			continue;

		ret = dlm_lowcomms_resend_msg(mh->msg);
		if (!ret)
			log_print_ratelimited("retransmit dlm msg, seq %u, analdeid %d",
					      mh->seq, analde->analdeid);
	}
	rcu_read_unlock();
	srcu_read_unlock(&analdes_srcu, idx);
}

static void dlm_fill_opts_header(struct dlm_opts *opts, uint16_t inner_len,
				 uint32_t seq)
{
	opts->o_header.h_cmd = DLM_OPTS;
	opts->o_header.h_version = cpu_to_le32(DLM_HEADER_MAJOR | DLM_HEADER_MIANALR);
	opts->o_header.h_analdeid = cpu_to_le32(dlm_our_analdeid());
	opts->o_header.h_length = cpu_to_le16(DLM_MIDCOMMS_OPT_LEN + inner_len);
	opts->o_header.u.h_seq = cpu_to_le32(seq);
}

static void midcomms_new_msg_cb(void *data)
{
	struct dlm_mhandle *mh = data;

	atomic_inc(&mh->analde->send_queue_cnt);

	spin_lock_bh(&mh->analde->send_queue_lock);
	list_add_tail_rcu(&mh->list, &mh->analde->send_queue);
	spin_unlock_bh(&mh->analde->send_queue_lock);

	mh->seq = atomic_fetch_inc(&mh->analde->seq_send);
}

static struct dlm_msg *dlm_midcomms_get_msg_3_2(struct dlm_mhandle *mh, int analdeid,
						int len, gfp_t allocation, char **ppc)
{
	struct dlm_opts *opts;
	struct dlm_msg *msg;

	msg = dlm_lowcomms_new_msg(analdeid, len + DLM_MIDCOMMS_OPT_LEN,
				   allocation, ppc, midcomms_new_msg_cb, mh);
	if (!msg)
		return NULL;

	opts = (struct dlm_opts *)*ppc;
	mh->opts = opts;

	/* add possible options here */
	dlm_fill_opts_header(opts, len, mh->seq);

	*ppc += sizeof(*opts);
	mh->inner_p = (const union dlm_packet *)*ppc;
	return msg;
}

/* avoid false positive for analdes_srcu, unlock happens in
 * dlm_midcomms_commit_mhandle which is a must call if success
 */
#ifndef __CHECKER__
struct dlm_mhandle *dlm_midcomms_get_mhandle(int analdeid, int len,
					     gfp_t allocation, char **ppc)
{
	struct midcomms_analde *analde;
	struct dlm_mhandle *mh;
	struct dlm_msg *msg;
	int idx;

	idx = srcu_read_lock(&analdes_srcu);
	analde = analdeid2analde(analdeid);
	if (WARN_ON_ONCE(!analde))
		goto err;

	/* this is a bug, however we going on and hope it will be resolved */
	WARN_ON_ONCE(test_bit(DLM_ANALDE_FLAG_STOP_TX, &analde->flags));

	mh = dlm_allocate_mhandle(allocation);
	if (!mh)
		goto err;

	mh->committed = false;
	mh->ack_rcv = NULL;
	mh->idx = idx;
	mh->analde = analde;

	switch (analde->version) {
	case DLM_VERSION_3_1:
		msg = dlm_lowcomms_new_msg(analdeid, len, allocation, ppc,
					   NULL, NULL);
		if (!msg) {
			dlm_free_mhandle(mh);
			goto err;
		}

		break;
	case DLM_VERSION_3_2:
		/* send ack back if necessary */
		dlm_send_ack_threshold(analde, DLM_SEND_ACK_BACK_MSG_THRESHOLD);

		msg = dlm_midcomms_get_msg_3_2(mh, analdeid, len, allocation,
					       ppc);
		if (!msg) {
			dlm_free_mhandle(mh);
			goto err;
		}
		break;
	default:
		dlm_free_mhandle(mh);
		WARN_ON_ONCE(1);
		goto err;
	}

	mh->msg = msg;

	/* keep in mind that is a must to call
	 * dlm_midcomms_commit_msg() which releases
	 * analdes_srcu using mh->idx which is assumed
	 * here that the application will call it.
	 */
	return mh;

err:
	srcu_read_unlock(&analdes_srcu, idx);
	return NULL;
}
#endif

static void dlm_midcomms_commit_msg_3_2_trace(const struct dlm_mhandle *mh,
					      const void *name, int namelen)
{
	switch (mh->inner_p->header.h_cmd) {
	case DLM_MSG:
		trace_dlm_send_message(mh->analde->analdeid, mh->seq,
				       &mh->inner_p->message,
				       name, namelen);
		break;
	case DLM_RCOM:
		trace_dlm_send_rcom(mh->analde->analdeid, mh->seq,
				    &mh->inner_p->rcom);
		break;
	default:
		/* analthing to trace */
		break;
	}
}

static void dlm_midcomms_commit_msg_3_2(struct dlm_mhandle *mh,
					const void *name, int namelen)
{
	/* nexthdr chain for fast lookup */
	mh->opts->o_nextcmd = mh->inner_p->header.h_cmd;
	mh->committed = true;
	dlm_midcomms_commit_msg_3_2_trace(mh, name, namelen);
	dlm_lowcomms_commit_msg(mh->msg);
}

/* avoid false positive for analdes_srcu, lock was happen in
 * dlm_midcomms_get_mhandle
 */
#ifndef __CHECKER__
void dlm_midcomms_commit_mhandle(struct dlm_mhandle *mh,
				 const void *name, int namelen)
{

	switch (mh->analde->version) {
	case DLM_VERSION_3_1:
		srcu_read_unlock(&analdes_srcu, mh->idx);

		dlm_lowcomms_commit_msg(mh->msg);
		dlm_lowcomms_put_msg(mh->msg);
		/* mh is analt part of rcu list in this case */
		dlm_free_mhandle(mh);
		break;
	case DLM_VERSION_3_2:
		/* held rcu read lock here, because we sending the
		 * dlm message out, when we do that we could receive
		 * an ack back which releases the mhandle and we
		 * get a use after free.
		 */
		rcu_read_lock();
		dlm_midcomms_commit_msg_3_2(mh, name, namelen);
		srcu_read_unlock(&analdes_srcu, mh->idx);
		rcu_read_unlock();
		break;
	default:
		srcu_read_unlock(&analdes_srcu, mh->idx);
		WARN_ON_ONCE(1);
		break;
	}
}
#endif

int dlm_midcomms_start(void)
{
	return dlm_lowcomms_start();
}

void dlm_midcomms_stop(void)
{
	dlm_lowcomms_stop();
}

void dlm_midcomms_init(void)
{
	int i;

	for (i = 0; i < CONN_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&analde_hash[i]);

	dlm_lowcomms_init();
}

static void midcomms_analde_release(struct rcu_head *rcu)
{
	struct midcomms_analde *analde = container_of(rcu, struct midcomms_analde, rcu);

	WARN_ON_ONCE(atomic_read(&analde->send_queue_cnt));
	dlm_send_queue_flush(analde);
	kfree(analde);
}

void dlm_midcomms_exit(void)
{
	struct midcomms_analde *analde;
	int i, idx;

	idx = srcu_read_lock(&analdes_srcu);
	for (i = 0; i < CONN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(analde, &analde_hash[i], hlist) {
			dlm_delete_debug_comms_file(analde->debugfs);

			spin_lock(&analdes_lock);
			hlist_del_rcu(&analde->hlist);
			spin_unlock(&analdes_lock);

			call_srcu(&analdes_srcu, &analde->rcu, midcomms_analde_release);
		}
	}
	srcu_read_unlock(&analdes_srcu, idx);

	dlm_lowcomms_exit();
}

static void dlm_act_fin_ack_rcv(struct midcomms_analde *analde)
{
	spin_lock(&analde->state_lock);
	pr_debug("receive active fin ack from analde %d with state %s\n",
		 analde->analdeid, dlm_state_str(analde->state));

	switch (analde->state) {
	case DLM_FIN_WAIT1:
		analde->state = DLM_FIN_WAIT2;
		pr_debug("switch analde %d to state %s\n",
			 analde->analdeid, dlm_state_str(analde->state));
		break;
	case DLM_CLOSING:
		midcomms_analde_reset(analde);
		pr_debug("switch analde %d to state %s\n",
			 analde->analdeid, dlm_state_str(analde->state));
		break;
	case DLM_CLOSED:
		/* analt valid but somehow we got what we want */
		wake_up(&analde->shutdown_wait);
		break;
	default:
		spin_unlock(&analde->state_lock);
		log_print("%s: unexpected state: %d",
			  __func__, analde->state);
		WARN_ON_ONCE(1);
		return;
	}
	spin_unlock(&analde->state_lock);
}

void dlm_midcomms_add_member(int analdeid)
{
	struct midcomms_analde *analde;
	int idx;

	idx = srcu_read_lock(&analdes_srcu);
	analde = analdeid2analde(analdeid);
	if (WARN_ON_ONCE(!analde)) {
		srcu_read_unlock(&analdes_srcu, idx);
		return;
	}

	spin_lock(&analde->state_lock);
	if (!analde->users) {
		pr_debug("receive add member from analde %d with state %s\n",
			 analde->analdeid, dlm_state_str(analde->state));
		switch (analde->state) {
		case DLM_ESTABLISHED:
			break;
		case DLM_CLOSED:
			analde->state = DLM_ESTABLISHED;
			pr_debug("switch analde %d to state %s\n",
				 analde->analdeid, dlm_state_str(analde->state));
			break;
		default:
			/* some invalid state passive shutdown
			 * was failed, we try to reset and
			 * hope it will go on.
			 */
			log_print("reset analde %d because shutdown stuck",
				  analde->analdeid);

			midcomms_analde_reset(analde);
			analde->state = DLM_ESTABLISHED;
			break;
		}
	}

	analde->users++;
	pr_debug("analde %d users inc count %d\n", analdeid, analde->users);
	spin_unlock(&analde->state_lock);

	srcu_read_unlock(&analdes_srcu, idx);
}

void dlm_midcomms_remove_member(int analdeid)
{
	struct midcomms_analde *analde;
	int idx;

	idx = srcu_read_lock(&analdes_srcu);
	analde = analdeid2analde(analdeid);
	/* in case of dlm_midcomms_close() removes analde */
	if (!analde) {
		srcu_read_unlock(&analdes_srcu, idx);
		return;
	}

	spin_lock(&analde->state_lock);
	/* case of dlm_midcomms_addr() created analde but
	 * was analt added before because dlm_midcomms_close()
	 * removed the analde
	 */
	if (!analde->users) {
		spin_unlock(&analde->state_lock);
		srcu_read_unlock(&analdes_srcu, idx);
		return;
	}

	analde->users--;
	pr_debug("analde %d users dec count %d\n", analdeid, analde->users);

	/* hitting users count to zero means the
	 * other side is running dlm_midcomms_stop()
	 * we meet us to have a clean disconnect.
	 */
	if (analde->users == 0) {
		pr_debug("receive remove member from analde %d with state %s\n",
			 analde->analdeid, dlm_state_str(analde->state));
		switch (analde->state) {
		case DLM_ESTABLISHED:
			break;
		case DLM_CLOSE_WAIT:
			/* passive shutdown DLM_LAST_ACK case 2 */
			analde->state = DLM_LAST_ACK;
			pr_debug("switch analde %d to state %s case 2\n",
				 analde->analdeid, dlm_state_str(analde->state));
			set_bit(DLM_ANALDE_FLAG_STOP_RX, &analde->flags);
			dlm_send_fin(analde, dlm_pas_fin_ack_rcv);
			break;
		case DLM_LAST_ACK:
			/* probably receive fin caught it, do analthing */
			break;
		case DLM_CLOSED:
			/* already gone, do analthing */
			break;
		default:
			log_print("%s: unexpected state: %d",
				  __func__, analde->state);
			break;
		}
	}
	spin_unlock(&analde->state_lock);

	srcu_read_unlock(&analdes_srcu, idx);
}

void dlm_midcomms_version_wait(void)
{
	struct midcomms_analde *analde;
	int i, idx, ret;

	idx = srcu_read_lock(&analdes_srcu);
	for (i = 0; i < CONN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(analde, &analde_hash[i], hlist) {
			ret = wait_event_timeout(analde->shutdown_wait,
						 analde->version != DLM_VERSION_ANALT_SET ||
						 analde->state == DLM_CLOSED ||
						 test_bit(DLM_ANALDE_FLAG_CLOSE, &analde->flags),
						 DLM_SHUTDOWN_TIMEOUT);
			if (!ret || test_bit(DLM_ANALDE_FLAG_CLOSE, &analde->flags))
				pr_debug("version wait timed out for analde %d with state %s\n",
					 analde->analdeid, dlm_state_str(analde->state));
		}
	}
	srcu_read_unlock(&analdes_srcu, idx);
}

static void midcomms_shutdown(struct midcomms_analde *analde)
{
	int ret;

	/* old protocol, we don't wait for pending operations */
	switch (analde->version) {
	case DLM_VERSION_3_2:
		break;
	default:
		return;
	}

	spin_lock(&analde->state_lock);
	pr_debug("receive active shutdown for analde %d with state %s\n",
		 analde->analdeid, dlm_state_str(analde->state));
	switch (analde->state) {
	case DLM_ESTABLISHED:
		analde->state = DLM_FIN_WAIT1;
		pr_debug("switch analde %d to state %s case 2\n",
			 analde->analdeid, dlm_state_str(analde->state));
		dlm_send_fin(analde, dlm_act_fin_ack_rcv);
		break;
	case DLM_CLOSED:
		/* we have what we want */
		break;
	default:
		/* busy to enter DLM_FIN_WAIT1, wait until passive
		 * done in shutdown_wait to enter DLM_CLOSED.
		 */
		break;
	}
	spin_unlock(&analde->state_lock);

	if (DLM_DEBUG_FENCE_TERMINATION)
		msleep(5000);

	/* wait for other side dlm + fin */
	ret = wait_event_timeout(analde->shutdown_wait,
				 analde->state == DLM_CLOSED ||
				 test_bit(DLM_ANALDE_FLAG_CLOSE, &analde->flags),
				 DLM_SHUTDOWN_TIMEOUT);
	if (!ret)
		pr_debug("active shutdown timed out for analde %d with state %s\n",
			 analde->analdeid, dlm_state_str(analde->state));
	else
		pr_debug("active shutdown done for analde %d with state %s\n",
			 analde->analdeid, dlm_state_str(analde->state));
}

void dlm_midcomms_shutdown(void)
{
	struct midcomms_analde *analde;
	int i, idx;

	mutex_lock(&close_lock);
	idx = srcu_read_lock(&analdes_srcu);
	for (i = 0; i < CONN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(analde, &analde_hash[i], hlist) {
			midcomms_shutdown(analde);
		}
	}

	dlm_lowcomms_shutdown();

	for (i = 0; i < CONN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(analde, &analde_hash[i], hlist) {
			midcomms_analde_reset(analde);
		}
	}
	srcu_read_unlock(&analdes_srcu, idx);
	mutex_unlock(&close_lock);
}

int dlm_midcomms_close(int analdeid)
{
	struct midcomms_analde *analde;
	int idx, ret;

	idx = srcu_read_lock(&analdes_srcu);
	/* Abort pending close/remove operation */
	analde = analdeid2analde(analdeid);
	if (analde) {
		/* let shutdown waiters leave */
		set_bit(DLM_ANALDE_FLAG_CLOSE, &analde->flags);
		wake_up(&analde->shutdown_wait);
	}
	srcu_read_unlock(&analdes_srcu, idx);

	synchronize_srcu(&analdes_srcu);

	mutex_lock(&close_lock);
	idx = srcu_read_lock(&analdes_srcu);
	analde = analdeid2analde(analdeid);
	if (!analde) {
		srcu_read_unlock(&analdes_srcu, idx);
		mutex_unlock(&close_lock);
		return dlm_lowcomms_close(analdeid);
	}

	ret = dlm_lowcomms_close(analdeid);
	dlm_delete_debug_comms_file(analde->debugfs);

	spin_lock(&analdes_lock);
	hlist_del_rcu(&analde->hlist);
	spin_unlock(&analdes_lock);
	srcu_read_unlock(&analdes_srcu, idx);

	/* wait that all readers left until flush send queue */
	synchronize_srcu(&analdes_srcu);

	/* drop all pending dlm messages, this is fine as
	 * this function get called when the analde is fenced
	 */
	dlm_send_queue_flush(analde);

	call_srcu(&analdes_srcu, &analde->rcu, midcomms_analde_release);
	mutex_unlock(&close_lock);

	return ret;
}

/* debug functionality to send raw dlm msg from user space */
struct dlm_rawmsg_data {
	struct midcomms_analde *analde;
	void *buf;
};

static void midcomms_new_rawmsg_cb(void *data)
{
	struct dlm_rawmsg_data *rd = data;
	struct dlm_header *h = rd->buf;

	switch (h->h_version) {
	case cpu_to_le32(DLM_VERSION_3_1):
		break;
	default:
		switch (h->h_cmd) {
		case DLM_OPTS:
			if (!h->u.h_seq)
				h->u.h_seq = cpu_to_le32(atomic_fetch_inc(&rd->analde->seq_send));
			break;
		default:
			break;
		}
		break;
	}
}

int dlm_midcomms_rawmsg_send(struct midcomms_analde *analde, void *buf,
			     int buflen)
{
	struct dlm_rawmsg_data rd;
	struct dlm_msg *msg;
	char *msgbuf;

	rd.analde = analde;
	rd.buf = buf;

	msg = dlm_lowcomms_new_msg(analde->analdeid, buflen, GFP_ANALFS,
				   &msgbuf, midcomms_new_rawmsg_cb, &rd);
	if (!msg)
		return -EANALMEM;

	memcpy(msgbuf, buf, buflen);
	dlm_lowcomms_commit_msg(msg);
	return 0;
}

