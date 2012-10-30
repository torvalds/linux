/* Linux ISDN subsystem, sync PPP, interface to ipppd
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 * Copyright 2000-2002  by Kai Germaschewski (kai@germaschewski.name)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */
#ifndef _LINUX_ISDN_PPP_H
#define _LINUX_ISDN_PPP_H




#ifdef CONFIG_IPPP_FILTER
#include <linux/filter.h>
#endif
#include <uapi/linux/isdn_ppp.h>

#define DECOMP_ERR_NOMEM	(-10)

#define MP_END_FRAG    0x40
#define MP_BEGIN_FRAG  0x80

#define MP_MAX_QUEUE_LEN	16

/*
 * We need a way for the decompressor to influence the generation of CCP
 * Reset-Requests in a variety of ways. The decompressor is already returning
 * a lot of information (generated skb length, error conditions) so we use
 * another parameter. This parameter is a pointer to a structure which is
 * to be marked valid by the decompressor and only in this case is ever used.
 * Furthermore, the only case where this data is used is when the decom-
 * pressor returns DECOMP_ERROR.
 *
 * We use this same struct for the reset entry of the compressor to commu-
 * nicate to its caller how to deal with sending of a Reset Ack. In this
 * case, expra is not used, but other options still apply (suppressing
 * sending with rsend, appending arbitrary data, etc).
 */

#define IPPP_RESET_MAXDATABYTES	32

struct isdn_ppp_resetparams {
  unsigned char valid:1;	/* rw Is this structure filled at all ? */
  unsigned char rsend:1;	/* rw Should we send one at all ? */
  unsigned char idval:1;	/* rw Is the id field valid ? */
  unsigned char dtval:1;	/* rw Is the data field valid ? */
  unsigned char expra:1;	/* rw Is an Ack expected for this Req ? */
  unsigned char id;		/* wo Send CCP ResetReq with this id */
  unsigned short maxdlen;	/* ro Max bytes to be stored in data field */
  unsigned short dlen;		/* rw Bytes stored in data field */
  unsigned char *data;		/* wo Data for ResetReq info field */
};

/*
 * this is an 'old friend' from ppp-comp.h under a new name 
 * check the original include for more information
 */
struct isdn_ppp_compressor {
  struct isdn_ppp_compressor *next, *prev;
  struct module *owner;
  int num; /* CCP compression protocol number */
  
  void *(*alloc) (struct isdn_ppp_comp_data *);
  void (*free) (void *state);
  int  (*init) (void *state, struct isdn_ppp_comp_data *,
		int unit,int debug);
  
  /* The reset entry needs to get more exact information about the
     ResetReq or ResetAck it was called with. The parameters are
     obvious. If reset is called without a Req or Ack frame which
     could be handed into it, code MUST be set to 0. Using rsparm,
     the reset entry can control if and how a ResetAck is returned. */
  
  void (*reset) (void *state, unsigned char code, unsigned char id,
		 unsigned char *data, unsigned len,
		 struct isdn_ppp_resetparams *rsparm);
  
  int  (*compress) (void *state, struct sk_buff *in,
		    struct sk_buff *skb_out, int proto);
  
	int  (*decompress) (void *state,struct sk_buff *in,
			    struct sk_buff *skb_out,
			    struct isdn_ppp_resetparams *rsparm);
  
  void (*incomp) (void *state, struct sk_buff *in,int proto);
  void (*stat) (void *state, struct compstat *stats);
};

extern int isdn_ppp_register_compressor(struct isdn_ppp_compressor *);
extern int isdn_ppp_unregister_compressor(struct isdn_ppp_compressor *);
extern int isdn_ppp_dial_slave(char *);
extern int isdn_ppp_hangup_slave(char *);

typedef struct {
  unsigned long seqerrs;
  unsigned long frame_drops;
  unsigned long overflows;
  unsigned long max_queue_len;
} isdn_mppp_stats;

typedef struct {
  int mp_mrru;                        /* unused                             */
  struct sk_buff * frags;	/* fragments sl list -- use skb->next */
  long frames;			/* number of frames in the frame list */
  unsigned int seq;		/* last processed packet seq #: any packets
  				 * with smaller seq # will be dropped
				 * unconditionally */
  spinlock_t lock;
  int ref_ct;				 
  /* statistics */
  isdn_mppp_stats stats;
} ippp_bundle;

#define NUM_RCV_BUFFS     64

struct ippp_buf_queue {
  struct ippp_buf_queue *next;
  struct ippp_buf_queue *last;
  char *buf;                 /* NULL here indicates end of queue */
  int len;
};

/* The data structure for one CCP reset transaction */
enum ippp_ccp_reset_states {
  CCPResetIdle,
  CCPResetSentReq,
  CCPResetRcvdReq,
  CCPResetSentAck,
  CCPResetRcvdAck
};

struct ippp_ccp_reset_state {
  enum ippp_ccp_reset_states state;	/* State of this transaction */
  struct ippp_struct *is;		/* Backlink to device stuff */
  unsigned char id;			/* Backlink id index */
  unsigned char ta:1;			/* The timer is active (flag) */
  unsigned char expra:1;		/* We expect a ResetAck at all */
  int dlen;				/* Databytes stored in data */
  struct timer_list timer;		/* For timeouts/retries */
  /* This is a hack but seems sufficient for the moment. We do not want
     to have this be yet another allocation for some bytes, it is more
     memory management overhead than the whole mess is worth. */
  unsigned char data[IPPP_RESET_MAXDATABYTES];
};

/* The data structure keeping track of the currently outstanding CCP Reset
   transactions. */
struct ippp_ccp_reset {
  struct ippp_ccp_reset_state *rs[256];	/* One per possible id */
  unsigned char lastid;			/* Last id allocated by the engine */
};

struct ippp_struct {
  struct ippp_struct *next_link;
  int state;
  spinlock_t buflock;
  struct ippp_buf_queue rq[NUM_RCV_BUFFS]; /* packet queue for isdn_ppp_read() */
  struct ippp_buf_queue *first;  /* pointer to (current) first packet */
  struct ippp_buf_queue *last;   /* pointer to (current) last used packet in queue */
  wait_queue_head_t wq;
  struct task_struct *tk;
  unsigned int mpppcfg;
  unsigned int pppcfg;
  unsigned int mru;
  unsigned int mpmru;
  unsigned int mpmtu;
  unsigned int maxcid;
  struct isdn_net_local_s *lp;
  int unit;
  int minor;
  unsigned int last_link_seqno;
  long mp_seqno;
#ifdef CONFIG_ISDN_PPP_VJ
  unsigned char *cbuf;
  struct slcompress *slcomp;
#endif
#ifdef CONFIG_IPPP_FILTER
  struct sock_filter *pass_filter;	/* filter for packets to pass */
  struct sock_filter *active_filter;	/* filter for pkts to reset idle */
  unsigned pass_len, active_len;
#endif
  unsigned long debug;
  struct isdn_ppp_compressor *compressor,*decompressor;
  struct isdn_ppp_compressor *link_compressor,*link_decompressor;
  void *decomp_stat,*comp_stat,*link_decomp_stat,*link_comp_stat;
  struct ippp_ccp_reset *reset;	/* Allocated on demand, may never be needed */
  unsigned long compflags;
};

#endif /* _LINUX_ISDN_PPP_H */
