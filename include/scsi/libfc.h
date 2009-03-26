/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _LIBFC_H_
#define _LIBFC_H_

#include <linux/timer.h>
#include <linux/if.h>

#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

#include <scsi/fc/fc_fcp.h>
#include <scsi/fc/fc_ns.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_gs.h>

#include <scsi/fc_frame.h>

#define LIBFC_DEBUG

#ifdef LIBFC_DEBUG
/* Log messages */
#define FC_DBG(fmt, args...)						\
	do {								\
		printk(KERN_INFO "%s " fmt, __func__, ##args);		\
	} while (0)
#else
#define FC_DBG(fmt, args...)
#endif

/*
 * libfc error codes
 */
#define	FC_NO_ERR	0	/* no error */
#define	FC_EX_TIMEOUT	1	/* Exchange timeout */
#define	FC_EX_CLOSED	2	/* Exchange closed */

/* some helpful macros */

#define ntohll(x) be64_to_cpu(x)
#define htonll(x) cpu_to_be64(x)

#define ntoh24(p) (((p)[0] << 16) | ((p)[1] << 8) | ((p)[2]))

#define hton24(p, v)	do {			\
		p[0] = (((v) >> 16) & 0xFF);	\
		p[1] = (((v) >> 8) & 0xFF);	\
		p[2] = ((v) & 0xFF);		\
	} while (0)

/*
 * FC HBA status
 */
enum fc_lport_state {
	LPORT_ST_NONE = 0,
	LPORT_ST_FLOGI,
	LPORT_ST_DNS,
	LPORT_ST_RPN_ID,
	LPORT_ST_RFT_ID,
	LPORT_ST_SCR,
	LPORT_ST_READY,
	LPORT_ST_LOGO,
	LPORT_ST_RESET
};

enum fc_disc_event {
	DISC_EV_NONE = 0,
	DISC_EV_SUCCESS,
	DISC_EV_FAILED
};

enum fc_rport_state {
	RPORT_ST_NONE = 0,
	RPORT_ST_INIT,		/* initialized */
	RPORT_ST_PLOGI,		/* waiting for PLOGI completion */
	RPORT_ST_PRLI,		/* waiting for PRLI completion */
	RPORT_ST_RTV,		/* waiting for RTV completion */
	RPORT_ST_READY,		/* ready for use */
	RPORT_ST_LOGO,		/* port logout sent */
};

enum fc_rport_trans_state {
	FC_PORTSTATE_ROGUE,
	FC_PORTSTATE_REAL,
};

/**
 * struct fc_disc_port - temporary discovery port to hold rport identifiers
 * @lp: Fibre Channel host port instance
 * @peers: node for list management during discovery and RSCN processing
 * @ids: identifiers structure to pass to fc_remote_port_add()
 * @rport_work: work struct for starting the rport state machine
 */
struct fc_disc_port {
	struct fc_lport             *lp;
	struct list_head            peers;
	struct fc_rport_identifiers ids;
	struct work_struct	    rport_work;
};

enum fc_rport_event {
	RPORT_EV_NONE = 0,
	RPORT_EV_CREATED,
	RPORT_EV_FAILED,
	RPORT_EV_STOP,
	RPORT_EV_LOGO
};

struct fc_rport_operations {
	void (*event_callback)(struct fc_lport *, struct fc_rport *,
			       enum fc_rport_event);
};

/**
 * struct fc_rport_libfc_priv - libfc internal information about a remote port
 * @local_port: Fibre Channel host port instance
 * @rp_state: state tracks progress of PLOGI, PRLI, and RTV exchanges
 * @flags: REC and RETRY supported flags
 * @max_seq: maximum number of concurrent sequences
 * @retries: retry count in current state
 * @e_d_tov: error detect timeout value (in msec)
 * @r_a_tov: resource allocation timeout value (in msec)
 * @rp_mutex: mutex protects rport
 * @retry_work:
 * @event_callback: Callback for rport READY, FAILED or LOGO
 */
struct fc_rport_libfc_priv {
	struct fc_lport		   *local_port;
	enum fc_rport_state        rp_state;
	u16			   flags;
	#define FC_RP_FLAGS_REC_SUPPORTED	(1 << 0)
	#define FC_RP_FLAGS_RETRY		(1 << 1)
	u16		           max_seq;
	unsigned int	           retries;
	unsigned int	           e_d_tov;
	unsigned int	           r_a_tov;
	enum fc_rport_trans_state  trans_state;
	struct mutex               rp_mutex;
	struct delayed_work	   retry_work;
	enum fc_rport_event        event;
	struct fc_rport_operations *ops;
	struct list_head           peers;
	struct work_struct         event_work;
};

#define PRIV_TO_RPORT(x)						\
	(struct fc_rport *)((void *)x - sizeof(struct fc_rport));
#define RPORT_TO_PRIV(x)						\
	(struct fc_rport_libfc_priv *)((void *)x + sizeof(struct fc_rport));

struct fc_rport *fc_rport_rogue_create(struct fc_disc_port *);

static inline void fc_rport_set_name(struct fc_rport *rport, u64 wwpn, u64 wwnn)
{
	rport->node_name = wwnn;
	rport->port_name = wwpn;
}

/*
 * fcoe stats structure
 */
struct fcoe_dev_stats {
	u64		SecondsSinceLastReset;
	u64		TxFrames;
	u64		TxWords;
	u64		RxFrames;
	u64		RxWords;
	u64		ErrorFrames;
	u64		DumpedFrames;
	u64		LinkFailureCount;
	u64		LossOfSignalCount;
	u64		InvalidTxWordCount;
	u64		InvalidCRCCount;
	u64		InputRequests;
	u64		OutputRequests;
	u64		ControlRequests;
	u64		InputMegabytes;
	u64		OutputMegabytes;
};

/*
 * els data is used for passing ELS respone specific
 * data to send ELS response mainly using infomation
 * in exchange and sequence in EM layer.
 */
struct fc_seq_els_data {
	struct fc_frame *fp;
	enum fc_els_rjt_reason reason;
	enum fc_els_rjt_explan explan;
};

/*
 * FCP request structure, one for each scsi cmd request
 */
struct fc_fcp_pkt {
	/*
	 * housekeeping stuff
	 */
	struct fc_lport *lp;	/* handle to hba struct */
	u16		state;		/* scsi_pkt state state */
	u16		tgt_flags;	/* target flags	 */
	atomic_t	ref_cnt;	/* fcp pkt ref count */
	spinlock_t	scsi_pkt_lock;	/* Must be taken before the host lock
					 * if both are held at the same time */
	/*
	 * SCSI I/O related stuff
	 */
	struct scsi_cmnd *cmd;		/* scsi command pointer. set/clear
					 * under host lock */
	struct list_head list;		/* tracks queued commands. access under
					 * host lock */
	/*
	 * timeout related stuff
	 */
	struct timer_list timer;	/* command timer */
	struct completion tm_done;
	int	wait_for_comp;
	unsigned long	start_time;	/* start jiffie */
	unsigned long	end_time;	/* end jiffie */
	unsigned long	last_pkt_time;	 /* jiffies of last frame received */

	/*
	 * scsi cmd and data transfer information
	 */
	u32		data_len;
	/*
	 * transport related veriables
	 */
	struct fcp_cmnd cdb_cmd;
	size_t		xfer_len;
	u32		xfer_contig_end; /* offset of end of contiguous xfer */
	u16		max_payload;	/* max payload size in bytes */

	/*
	 * scsi/fcp return status
	 */
	u32		io_status;	/* SCSI result upper 24 bits */
	u8		cdb_status;
	u8		status_code;	/* FCP I/O status */
	/* bit 3 Underrun bit 2: overrun */
	u8		scsi_comp_flags;
	u32		req_flags;	/* bit 0: read bit:1 write */
	u32		scsi_resid;	/* residule length */

	struct fc_rport	*rport;		/* remote port pointer */
	struct fc_seq	*seq_ptr;	/* current sequence pointer */
	/*
	 * Error Processing
	 */
	u8		recov_retry;	/* count of recovery retries */
	struct fc_seq	*recov_seq;	/* sequence for REC or SRR */
};

/*
 * Structure and function definitions for managing Fibre Channel Exchanges
 * and Sequences
 *
 * fc_exch holds state for one exchange and links to its active sequence.
 *
 * fc_seq holds the state for an individual sequence.
 */

struct fc_exch_mgr;

/*
 * Sequence.
 */
struct fc_seq {
	u8	id;		/* seq ID */
	u16	ssb_stat;	/* status flags for sequence status block */
	u16	cnt;		/* frames sent so far on sequence */
	u32	rec_data;	/* FC-4 value for REC */
};

#define FC_EX_DONE		(1 << 0) /* ep is completed */
#define FC_EX_RST_CLEANUP	(1 << 1) /* reset is forcing completion */

/*
 * Exchange.
 *
 * Locking notes: The ex_lock protects following items:
 *	state, esb_stat, f_ctl, seq.ssb_stat
 *	seq_id
 *	sequence allocation
 */
struct fc_exch {
	struct fc_exch_mgr *em;		/* exchange manager */
	u32		state;		/* internal driver state */
	u16		xid;		/* our exchange ID */
	struct list_head	ex_list;	/* free or busy list linkage */
	spinlock_t	ex_lock;	/* lock covering exchange state */
	atomic_t	ex_refcnt;	/* reference counter */
	struct delayed_work timeout_work; /* timer for upper level protocols */
	struct fc_lport	*lp;		/* fc device instance */
	u16		oxid;		/* originator's exchange ID */
	u16		rxid;		/* responder's exchange ID */
	u32		oid;		/* originator's FCID */
	u32		sid;		/* source FCID */
	u32		did;		/* destination FCID */
	u32		esb_stat;	/* exchange status for ESB */
	u32		r_a_tov;	/* r_a_tov from rport (msec) */
	u8		seq_id;		/* next sequence ID to use */
	u32		f_ctl;		/* F_CTL flags for sequences */
	u8		fh_type;	/* frame type */
	enum fc_class	class;		/* class of service */
	struct fc_seq	seq;		/* single sequence */
	/*
	 * Handler for responses to this current exchange.
	 */
	void		(*resp)(struct fc_seq *, struct fc_frame *, void *);
	void		(*destructor)(struct fc_seq *, void *);
	/*
	 * arg is passed as void pointer to exchange
	 * resp and destructor handlers
	 */
	void		*arg;
};
#define	fc_seq_exch(sp) container_of(sp, struct fc_exch, seq)

struct libfc_function_template {

	/*
	 * Interface to send a FC frame
	 *
	 * STATUS: REQUIRED
	 */
	int (*frame_send)(struct fc_lport *lp, struct fc_frame *fp);

	/*
	 * Interface to send ELS/CT frames
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_seq *(*elsct_send)(struct fc_lport *lport,
				     struct fc_rport *rport,
				     struct fc_frame *fp,
				     unsigned int op,
				     void (*resp)(struct fc_seq *,
					     struct fc_frame *fp,
					     void *arg),
				     void *arg, u32 timer_msec);

	/*
	 * Send the FC frame payload using a new exchange and sequence.
	 *
	 * The frame pointer with some of the header's fields must be
	 * filled before calling exch_seq_send(), those fields are,
	 *
	 * - routing control
	 * - FC port did
	 * - FC port sid
	 * - FC header type
	 * - frame control
	 * - parameter or relative offset
	 *
	 * The exchange response handler is set in this routine to resp()
	 * function pointer. It can be called in two scenarios: if a timeout
	 * occurs or if a response frame is received for the exchange. The
	 * fc_frame pointer in response handler will also indicate timeout
	 * as error using IS_ERR related macros.
	 *
	 * The exchange destructor handler is also set in this routine.
	 * The destructor handler is invoked by EM layer when exchange
	 * is about to free, this can be used by caller to free its
	 * resources along with exchange free.
	 *
	 * The arg is passed back to resp and destructor handler.
	 *
	 * The timeout value (in msec) for an exchange is set if non zero
	 * timer_msec argument is specified. The timer is canceled when
	 * it fires or when the exchange is done. The exchange timeout handler
	 * is registered by EM layer.
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_seq *(*exch_seq_send)(struct fc_lport *lp,
					struct fc_frame *fp,
					void (*resp)(struct fc_seq *sp,
						     struct fc_frame *fp,
						     void *arg),
					void (*destructor)(struct fc_seq *sp,
							   void *arg),
					void *arg, unsigned int timer_msec);

	/*
	 * Send a frame using an existing sequence and exchange.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*seq_send)(struct fc_lport *lp, struct fc_seq *sp,
			struct fc_frame *fp);

	/*
	 * Send an ELS response using infomation from a previous
	 * exchange and sequence.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*seq_els_rsp_send)(struct fc_seq *sp, enum fc_els_cmd els_cmd,
				 struct fc_seq_els_data *els_data);

	/*
	 * Abort an exchange and sequence. Generally called because of a
	 * exchange timeout or an abort from the upper layer.
	 *
	 * A timer_msec can be specified for abort timeout, if non-zero
	 * timer_msec value is specified then exchange resp handler
	 * will be called with timeout error if no response to abort.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*seq_exch_abort)(const struct fc_seq *req_sp,
			      unsigned int timer_msec);

	/*
	 * Indicate that an exchange/sequence tuple is complete and the memory
	 * allocated for the related objects may be freed.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*exch_done)(struct fc_seq *sp);

	/*
	 * Assigns a EM and a free XID for an new exchange and then
	 * allocates a new exchange and sequence pair.
	 * The fp can be used to determine free XID.
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_exch *(*exch_get)(struct fc_lport *lp, struct fc_frame *fp);

	/*
	 * Release previously assigned XID by exch_get API.
	 * The LLD may implement this if XID is assigned by LLD
	 * in exch_get().
	 *
	 * STATUS: OPTIONAL
	 */
	void (*exch_put)(struct fc_lport *lp, struct fc_exch_mgr *mp,
			 u16 ex_id);

	/*
	 * Start a new sequence on the same exchange/sequence tuple.
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_seq *(*seq_start_next)(struct fc_seq *sp);

	/*
	 * Reset an exchange manager, completing all sequences and exchanges.
	 * If s_id is non-zero, reset only exchanges originating from that FID.
	 * If d_id is non-zero, reset only exchanges sending to that FID.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*exch_mgr_reset)(struct fc_lport *,
			       u32 s_id, u32 d_id);

	/*
	 * Flush the rport work queue. Generally used before shutdown.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*rport_flush_queue)(void);

	/*
	 * Receive a frame for a local port.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*lport_recv)(struct fc_lport *lp, struct fc_seq *sp,
			   struct fc_frame *fp);

	/*
	 * Reset the local port.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*lport_reset)(struct fc_lport *);

	/*
	 * Create a remote port
	 */
	struct fc_rport *(*rport_create)(struct fc_disc_port *);

	/*
	 * Initiates the RP state machine. It is called from the LP module.
	 * This function will issue the following commands to the N_Port
	 * identified by the FC ID provided.
	 *
	 * - PLOGI
	 * - PRLI
	 * - RTV
	 *
	 * STATUS: OPTIONAL
	 */
	int (*rport_login)(struct fc_rport *rport);

	/*
	 * Logoff, and remove the rport from the transport if
	 * it had been added. This will send a LOGO to the target.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*rport_logoff)(struct fc_rport *rport);

	/*
	 * Recieve a request from a remote port.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*rport_recv_req)(struct fc_seq *, struct fc_frame *,
			       struct fc_rport *);

	/*
	 * lookup an rport by it's port ID.
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_rport *(*rport_lookup)(const struct fc_lport *, u32);

	/*
	 * Send a fcp cmd from fsp pkt.
	 * Called with the SCSI host lock unlocked and irqs disabled.
	 *
	 * The resp handler is called when FCP_RSP received.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*fcp_cmd_send)(struct fc_lport *lp, struct fc_fcp_pkt *fsp,
			    void (*resp)(struct fc_seq *, struct fc_frame *fp,
					 void *arg));

	/*
	 * Cleanup the FCP layer, used durring link down and reset
	 *
	 * STATUS: OPTIONAL
	 */
	void (*fcp_cleanup)(struct fc_lport *lp);

	/*
	 * Abort all I/O on a local port
	 *
	 * STATUS: OPTIONAL
	 */
	void (*fcp_abort_io)(struct fc_lport *lp);

	/*
	 * Receive a request for the discovery layer.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*disc_recv_req)(struct fc_seq *,
			      struct fc_frame *, struct fc_lport *);

	/*
	 * Start discovery for a local port.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*disc_start)(void (*disc_callback)(struct fc_lport *,
						 enum fc_disc_event),
			   struct fc_lport *);

	/*
	 * Stop discovery for a given lport. This will remove
	 * all discovered rports
	 *
	 * STATUS: OPTIONAL
	 */
	void (*disc_stop) (struct fc_lport *);

	/*
	 * Stop discovery for a given lport. This will block
	 * until all discovered rports are deleted from the
	 * FC transport class
	 *
	 * STATUS: OPTIONAL
	 */
	void (*disc_stop_final) (struct fc_lport *);
};

/* information used by the discovery layer */
struct fc_disc {
	unsigned char		retry_count;
	unsigned char		delay;
	unsigned char		pending;
	unsigned char		requested;
	unsigned short		seq_count;
	unsigned char		buf_len;
	enum fc_disc_event	event;

	void (*disc_callback)(struct fc_lport *,
			      enum fc_disc_event);

	struct list_head	 rports;
	struct fc_lport		*lport;
	struct mutex		disc_mutex;
	struct fc_gpn_ft_resp	partial_buf;	/* partial name buffer */
	struct delayed_work	disc_work;
};

struct fc_lport {
	struct list_head list;

	/* Associations */
	struct Scsi_Host	*host;
	struct fc_exch_mgr	*emp;
	struct fc_rport		*dns_rp;
	struct fc_rport		*ptp_rp;
	void			*scsi_priv;
	struct fc_disc          disc;

	/* Operational Information */
	struct libfc_function_template tt;
	u8			link_up;
	u8			qfull;
	enum fc_lport_state	state;
	unsigned long		boot_time;

	struct fc_host_statistics host_stats;
	struct fcoe_dev_stats	*dev_stats[NR_CPUS];
	u64			wwpn;
	u64			wwnn;
	u8			retry_count;

	/* Capabilities */
	u32			sg_supp:1;	/* scatter gather supported */
	u32			seq_offload:1;	/* seq offload supported */
	u32			crc_offload:1;	/* crc offload supported */
	u32			lro_enabled:1;	/* large receive offload */
	u32			mfs;	        /* max FC payload size */
	unsigned int		service_params;
	unsigned int		e_d_tov;
	unsigned int		r_a_tov;
	u8			max_retry_count;
	u16			link_speed;
	u16			link_supported_speeds;
	u16			lro_xid;	/* max xid for fcoe lro */
	struct fc_ns_fts	fcts;	        /* FC-4 type masks */
	struct fc_els_rnid_gen	rnid_gen;	/* RNID information */

	/* Semaphores */
	struct mutex lp_mutex;

	/* Miscellaneous */
	struct delayed_work	retry_work;
	struct delayed_work	disc_work;
};

/*
 * FC_LPORT HELPER FUNCTIONS
 *****************************/
static inline void *lport_priv(const struct fc_lport *lp)
{
	return (void *)(lp + 1);
}

static inline int fc_lport_test_ready(struct fc_lport *lp)
{
	return lp->state == LPORT_ST_READY;
}

static inline void fc_set_wwnn(struct fc_lport *lp, u64 wwnn)
{
	lp->wwnn = wwnn;
}

static inline void fc_set_wwpn(struct fc_lport *lp, u64 wwnn)
{
	lp->wwpn = wwnn;
}

static inline void fc_lport_state_enter(struct fc_lport *lp,
					enum fc_lport_state state)
{
	if (state != lp->state)
		lp->retry_count = 0;
	lp->state = state;
}


/*
 * LOCAL PORT LAYER
 *****************************/
int fc_lport_init(struct fc_lport *lp);

/*
 * Destroy the specified local port by finding and freeing all
 * fc_rports associated with it and then by freeing the fc_lport
 * itself.
 */
int fc_lport_destroy(struct fc_lport *lp);

/*
 * Logout the specified local port from the fabric
 */
int fc_fabric_logoff(struct fc_lport *lp);

/*
 * Initiate the LP state machine. This handler will use fc_host_attr
 * to store the FLOGI service parameters, so fc_host_attr must be
 * initialized before calling this handler.
 */
int fc_fabric_login(struct fc_lport *lp);

/*
 * The link is up for the given local port.
 */
void fc_linkup(struct fc_lport *);

/*
 * Link is down for the given local port.
 */
void fc_linkdown(struct fc_lport *);

/*
 * Configure the local port.
 */
int fc_lport_config(struct fc_lport *);

/*
 * Reset the local port.
 */
int fc_lport_reset(struct fc_lport *);

/*
 * Set the mfs or reset
 */
int fc_set_mfs(struct fc_lport *lp, u32 mfs);


/*
 * REMOTE PORT LAYER
 *****************************/
int fc_rport_init(struct fc_lport *lp);
void fc_rport_terminate_io(struct fc_rport *rp);

/*
 * DISCOVERY LAYER
 *****************************/
int fc_disc_init(struct fc_lport *lp);


/*
 * SCSI LAYER
 *****************************/
/*
 * Initialize the SCSI block of libfc
 */
int fc_fcp_init(struct fc_lport *);

/*
 * This section provides an API which allows direct interaction
 * with the SCSI-ml. Each of these functions satisfies a function
 * pointer defined in Scsi_Host and therefore is always called
 * directly from the SCSI-ml.
 */
int fc_queuecommand(struct scsi_cmnd *sc_cmd,
		    void (*done)(struct scsi_cmnd *));

/*
 * complete processing of a fcp packet
 *
 * This function may sleep if a fsp timer is pending.
 * The host lock must not be held by caller.
 */
void fc_fcp_complete(struct fc_fcp_pkt *fsp);

/*
 * Send an ABTS frame to the target device. The sc_cmd argument
 * is a pointer to the SCSI command to be aborted.
 */
int fc_eh_abort(struct scsi_cmnd *sc_cmd);

/*
 * Reset a LUN by sending send the tm cmd to the target.
 */
int fc_eh_device_reset(struct scsi_cmnd *sc_cmd);

/*
 * Reset the host adapter.
 */
int fc_eh_host_reset(struct scsi_cmnd *sc_cmd);

/*
 * Check rport status.
 */
int fc_slave_alloc(struct scsi_device *sdev);

/*
 * Adjust the queue depth.
 */
int fc_change_queue_depth(struct scsi_device *sdev, int qdepth);

/*
 * Change the tag type.
 */
int fc_change_queue_type(struct scsi_device *sdev, int tag_type);

/*
 * Free memory pools used by the FCP layer.
 */
void fc_fcp_destroy(struct fc_lport *);

/*
 * ELS/CT interface
 *****************************/
/*
 * Initializes ELS/CT interface
 */
int fc_elsct_init(struct fc_lport *lp);


/*
 * EXCHANGE MANAGER LAYER
 *****************************/
/*
 * Initializes Exchange Manager related
 * function pointers in struct libfc_function_template.
 */
int fc_exch_init(struct fc_lport *lp);

/*
 * Allocates an Exchange Manager (EM).
 *
 * The EM manages exchanges for their allocation and
 * free, also allows exchange lookup for received
 * frame.
 *
 * The class is used for initializing FC class of
 * allocated exchange from EM.
 *
 * The min_xid and max_xid will limit new
 * exchange ID (XID) within this range for
 * a new exchange.
 * The LLD may choose to have multiple EMs,
 * e.g. one EM instance per CPU receive thread in LLD.
 * The LLD can use exch_get() of struct libfc_function_template
 * to specify XID for a new exchange within
 * a specified EM instance.
 *
 * The em_idx to uniquely identify an EM instance.
 */
struct fc_exch_mgr *fc_exch_mgr_alloc(struct fc_lport *lp,
				      enum fc_class class,
				      u16 min_xid,
				      u16 max_xid);

/*
 * Free an exchange manager.
 */
void fc_exch_mgr_free(struct fc_exch_mgr *mp);

/*
 * Receive a frame on specified local port and exchange manager.
 */
void fc_exch_recv(struct fc_lport *lp, struct fc_exch_mgr *mp,
		  struct fc_frame *fp);

/*
 * This function is for exch_seq_send function pointer in
 * struct libfc_function_template, see comment block on
 * exch_seq_send for description of this function.
 */
struct fc_seq *fc_exch_seq_send(struct fc_lport *lp,
				struct fc_frame *fp,
				void (*resp)(struct fc_seq *sp,
					     struct fc_frame *fp,
					     void *arg),
				void (*destructor)(struct fc_seq *sp,
						   void *arg),
				void *arg, u32 timer_msec);

/*
 * send a frame using existing sequence and exchange.
 */
int fc_seq_send(struct fc_lport *lp, struct fc_seq *sp, struct fc_frame *fp);

/*
 * Send ELS response using mainly infomation
 * in exchange and sequence in EM layer.
 */
void fc_seq_els_rsp_send(struct fc_seq *sp, enum fc_els_cmd els_cmd,
			 struct fc_seq_els_data *els_data);

/*
 * This function is for seq_exch_abort function pointer in
 * struct libfc_function_template, see comment block on
 * seq_exch_abort for description of this function.
 */
int fc_seq_exch_abort(const struct fc_seq *req_sp, unsigned int timer_msec);

/*
 * Indicate that an exchange/sequence tuple is complete and the memory
 * allocated for the related objects may be freed.
 */
void fc_exch_done(struct fc_seq *sp);

/*
 * Assigns a EM and XID for a frame and then allocates
 * a new exchange and sequence pair.
 * The fp can be used to determine free XID.
 */
struct fc_exch *fc_exch_get(struct fc_lport *lp, struct fc_frame *fp);

/*
 * Allocate a new exchange and sequence pair.
 * if ex_id is zero then next free exchange id
 * from specified exchange manger mp will be assigned.
 */
struct fc_exch *fc_exch_alloc(struct fc_exch_mgr *mp,
			      struct fc_frame *fp, u16 ex_id);
/*
 * Start a new sequence on the same exchange as the supplied sequence.
 */
struct fc_seq *fc_seq_start_next(struct fc_seq *sp);

/*
 * Reset an exchange manager, completing all sequences and exchanges.
 * If s_id is non-zero, reset only exchanges originating from that FID.
 * If d_id is non-zero, reset only exchanges sending to that FID.
 */
void fc_exch_mgr_reset(struct fc_lport *, u32 s_id, u32 d_id);

/*
 * Functions for fc_functions_template
 */
void fc_get_host_speed(struct Scsi_Host *shost);
void fc_get_host_port_type(struct Scsi_Host *shost);
void fc_get_host_port_state(struct Scsi_Host *shost);
void fc_set_rport_loss_tmo(struct fc_rport *rport, u32 timeout);
struct fc_host_statistics *fc_get_host_stats(struct Scsi_Host *);

/*
 * module setup functions.
 */
int fc_setup_exch_mgr(void);
void fc_destroy_exch_mgr(void);
int fc_setup_rport(void);
void fc_destroy_rport(void);

#endif /* _LIBFC_H_ */
