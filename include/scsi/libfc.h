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
#include <linux/percpu.h>

#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_bsg_fc.h>

#include <scsi/fc/fc_fcp.h>
#include <scsi/fc/fc_ns.h>
#include <scsi/fc/fc_ms.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_gs.h>

#include <scsi/fc_frame.h>

#define	FC_FC4_PROV_SIZE	(FC_TYPE_FCP + 1)	/* size of tables */

/*
 * libfc error codes
 */
#define	FC_NO_ERR	0	/* no error */
#define	FC_EX_TIMEOUT	1	/* Exchange timeout */
#define	FC_EX_CLOSED	2	/* Exchange closed */

/**
 * enum fc_lport_state - Local port states
 * @LPORT_ST_DISABLED: Disabled
 * @LPORT_ST_FLOGI:    Fabric login (FLOGI) sent
 * @LPORT_ST_DNS:      Waiting for name server remote port to become ready
 * @LPORT_ST_RPN_ID:   Register port name by ID (RPN_ID) sent
 * @LPORT_ST_RFT_ID:   Register Fibre Channel types by ID (RFT_ID) sent
 * @LPORT_ST_RFF_ID:   Register FC-4 Features by ID (RFF_ID) sent
 * @LPORT_ST_FDMI:     Waiting for mgmt server rport to become ready
 * @LPORT_ST_RHBA:
 * @LPORT_ST_SCR:      State Change Register (SCR) sent
 * @LPORT_ST_READY:    Ready for use
 * @LPORT_ST_LOGO:     Local port logout (LOGO) sent
 * @LPORT_ST_RESET:    Local port reset
 */
enum fc_lport_state {
	LPORT_ST_DISABLED = 0,
	LPORT_ST_FLOGI,
	LPORT_ST_DNS,
	LPORT_ST_RNN_ID,
	LPORT_ST_RSNN_NN,
	LPORT_ST_RSPN_ID,
	LPORT_ST_RFT_ID,
	LPORT_ST_RFF_ID,
	LPORT_ST_FDMI,
	LPORT_ST_RHBA,
	LPORT_ST_RPA,
	LPORT_ST_DHBA,
	LPORT_ST_DPRT,
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

/**
 * enum fc_rport_state - Remote port states
 * @RPORT_ST_INIT:    Initialized
 * @RPORT_ST_FLOGI:   Waiting for FLOGI completion for point-to-multipoint
 * @RPORT_ST_PLOGI_WAIT:   Waiting for peer to login for point-to-multipoint
 * @RPORT_ST_PLOGI:   Waiting for PLOGI completion
 * @RPORT_ST_PRLI:    Waiting for PRLI completion
 * @RPORT_ST_RTV:     Waiting for RTV completion
 * @RPORT_ST_READY:   Ready for use
 * @RPORT_ST_ADISC:   Discover Address sent
 * @RPORT_ST_DELETE:  Remote port being deleted
*/
enum fc_rport_state {
	RPORT_ST_INIT,
	RPORT_ST_FLOGI,
	RPORT_ST_PLOGI_WAIT,
	RPORT_ST_PLOGI,
	RPORT_ST_PRLI,
	RPORT_ST_RTV,
	RPORT_ST_READY,
	RPORT_ST_ADISC,
	RPORT_ST_DELETE,
};

/**
 * struct fc_disc_port - temporary discovery port to hold rport identifiers
 * @lp:         Fibre Channel host port instance
 * @peers:      Node for list management during discovery and RSCN processing
 * @rport_work: Work struct for starting the rport state machine
 * @port_id:    Port ID of the discovered port
 */
struct fc_disc_port {
	struct fc_lport    *lp;
	struct list_head   peers;
	struct work_struct rport_work;
	u32                port_id;
};

/**
 * enum fc_rport_event - Remote port events
 * @RPORT_EV_NONE:   No event
 * @RPORT_EV_READY:  Remote port is ready for use
 * @RPORT_EV_FAILED: State machine failed, remote port is not ready
 * @RPORT_EV_STOP:   Remote port has been stopped
 * @RPORT_EV_LOGO:   Remote port logout (LOGO) sent
 */
enum fc_rport_event {
	RPORT_EV_NONE = 0,
	RPORT_EV_READY,
	RPORT_EV_FAILED,
	RPORT_EV_STOP,
	RPORT_EV_LOGO
};

struct fc_rport_priv;

/**
 * struct fc_rport_operations - Operations for a remote port
 * @event_callback: Function to be called for remote port events
 */
struct fc_rport_operations {
	void (*event_callback)(struct fc_lport *, struct fc_rport_priv *,
			       enum fc_rport_event);
};

/**
 * struct fc_rport_libfc_priv - libfc internal information about a remote port
 * @local_port: The associated local port
 * @rp_state:   Indicates READY for I/O or DELETE when blocked
 * @flags:      REC and RETRY supported flags
 * @e_d_tov:    Error detect timeout value (in msec)
 * @r_a_tov:    Resource allocation timeout value (in msec)
 */
struct fc_rport_libfc_priv {
	struct fc_lport		   *local_port;
	enum fc_rport_state        rp_state;
	u16			   flags;
	#define FC_RP_FLAGS_REC_SUPPORTED	(1 << 0)
	#define FC_RP_FLAGS_RETRY		(1 << 1)
	#define FC_RP_STARTED			(1 << 2)
	#define FC_RP_FLAGS_CONF_REQ		(1 << 3)
	unsigned int	           e_d_tov;
	unsigned int	           r_a_tov;
};

/**
 * struct fc_rport_priv - libfc remote port and discovery info
 * @local_port:     The associated local port
 * @rport:          The FC transport remote port
 * @kref:           Reference counter
 * @rp_state:       Enumeration that tracks progress of PLOGI, PRLI,
 *                  and RTV exchanges
 * @ids:            The remote port identifiers and roles
 * @flags:          STARTED, REC and RETRY_SUPPORTED flags
 * @max_seq:        Maximum number of concurrent sequences
 * @disc_id:        The discovery identifier
 * @maxframe_size:  The maximum frame size
 * @retries:        The retry count for the current state
 * @major_retries:  The retry count for the entire PLOGI/PRLI state machine
 * @e_d_tov:        Error detect timeout value (in msec)
 * @r_a_tov:        Resource allocation timeout value (in msec)
 * @rp_mutex:       The mutex that protects the remote port
 * @retry_work:     Handle for retries
 * @event_callback: Callback when READY, FAILED or LOGO states complete
 * @prli_count:     Count of open PRLI sessions in providers
 * @rcu:	    Structure used for freeing in an RCU-safe manner
 */
struct fc_rport_priv {
	struct fc_lport		    *local_port;
	struct fc_rport		    *rport;
	struct kref		    kref;
	enum fc_rport_state         rp_state;
	struct fc_rport_identifiers ids;
	u16			    flags;
	u16		            max_seq;
	u16			    disc_id;
	u16			    maxframe_size;
	unsigned int	            retries;
	unsigned int	            major_retries;
	unsigned int	            e_d_tov;
	unsigned int	            r_a_tov;
	struct mutex                rp_mutex;
	struct delayed_work	    retry_work;
	enum fc_rport_event         event;
	struct fc_rport_operations  *ops;
	struct list_head            peers;
	struct work_struct          event_work;
	u32			    supported_classes;
	u16                         prli_count;
	struct rcu_head		    rcu;
	u16			    sp_features;
	u8			    spp_type;
	void			    (*lld_event_callback)(struct fc_lport *,
						      struct fc_rport_priv *,
						      enum fc_rport_event);
};

/**
 * struct fc_stats - fc stats structure
 * @SecondsSinceLastReset: Seconds since the last reset
 * @TxFrames:              Number of transmitted frames
 * @TxWords:               Number of transmitted words
 * @RxFrames:              Number of received frames
 * @RxWords:               Number of received words
 * @ErrorFrames:           Number of received error frames
 * @DumpedFrames:          Number of dumped frames
 * @FcpPktAllocFails:      Number of fcp packet allocation failures
 * @FcpPktAborts:          Number of fcp packet aborts
 * @FcpFrameAllocFails:    Number of fcp frame allocation failures
 * @LinkFailureCount:      Number of link failures
 * @LossOfSignalCount:     Number for signal losses
 * @InvalidTxWordCount:    Number of invalid transmitted words
 * @InvalidCRCCount:       Number of invalid CRCs
 * @InputRequests:         Number of input requests
 * @OutputRequests:        Number of output requests
 * @ControlRequests:       Number of control requests
 * @InputBytes:            Number of received bytes
 * @OutputBytes:           Number of transmitted bytes
 * @VLinkFailureCount:     Number of virtual link failures
 * @MissDiscAdvCount:      Number of missing FIP discovery advertisement
 */
struct fc_stats {
	u64		SecondsSinceLastReset;
	u64		TxFrames;
	u64		TxWords;
	u64		RxFrames;
	u64		RxWords;
	u64		ErrorFrames;
	u64		DumpedFrames;
	u64		FcpPktAllocFails;
	u64		FcpPktAborts;
	u64		FcpFrameAllocFails;
	u64		LinkFailureCount;
	u64		LossOfSignalCount;
	u64		InvalidTxWordCount;
	u64		InvalidCRCCount;
	u64		InputRequests;
	u64		OutputRequests;
	u64		ControlRequests;
	u64		InputBytes;
	u64		OutputBytes;
	u64		VLinkFailureCount;
	u64		MissDiscAdvCount;
};

/**
 * struct fc_seq_els_data - ELS data used for passing ELS specific responses
 * @reason: The reason for rejection
 * @explan: The explanation of the rejection
 *
 * Mainly used by the exchange manager layer.
 */
struct fc_seq_els_data {
	enum fc_els_rjt_reason reason;
	enum fc_els_rjt_explan explan;
};

/**
 * struct fc_fcp_pkt - FCP request structure (one for each scsi_cmnd request)
 * @lp:              The associated local port
 * @state:           The state of the I/O
 * @ref_cnt:         Reference count
 * @scsi_pkt_lock:   Lock to protect the SCSI packet (must be taken before the
 *                   host_lock if both are to be held at the same time)
 * @cmd:             The SCSI command (set and clear with the host_lock held)
 * @list:            Tracks queued commands (accessed with the host_lock held)
 * @timer:           The command timer
 * @tm_done:         Completion indicator
 * @wait_for_comp:   Indicator to wait for completion of the I/O (in jiffies)
 * @data_len:        The length of the data
 * @cdb_cmd:         The CDB command
 * @xfer_len:        The transfer length
 * @xfer_ddp:        Indicates if this transfer used DDP (XID of the exchange
 *                   will be set here if DDP was setup)
 * @xfer_contig_end: The offset into the buffer if the buffer is contiguous
 *                   (Tx and Rx)
 * @max_payload:     The maximum payload size (in bytes)
 * @io_status:       SCSI result (upper 24 bits)
 * @cdb_status:      CDB status
 * @status_code:     FCP I/O status
 * @scsi_comp_flags: Completion flags (bit 3 Underrun bit 2: overrun)
 * @req_flags:       Request flags (bit 0: read bit:1 write)
 * @scsi_resid:      SCSI residule length
 * @rport:           The remote port that the SCSI command is targeted at
 * @seq_ptr:         The sequence that will carry the SCSI command
 * @recov_retry:     Number of recovery retries
 * @recov_seq:       The sequence for REC or SRR
 */
struct fc_fcp_pkt {
	spinlock_t	  scsi_pkt_lock;
	atomic_t	  ref_cnt;

	/* SCSI command and data transfer information */
	u32		  data_len;

	/* SCSI I/O related information */
	struct scsi_cmnd  *cmd;
	struct list_head  list;

	/* Housekeeping information */
	struct fc_lport   *lp;
	u8		  state;

	/* SCSI/FCP return status */
	u8		  cdb_status;
	u8		  status_code;
	u8		  scsi_comp_flags;
	u32		  io_status;
	u32		  req_flags;
	u32		  scsi_resid;

	/* Transport related veriables */
	size_t		  xfer_len;
	struct fcp_cmnd   cdb_cmd;
	u32		  xfer_contig_end;
	u16		  max_payload;
	u16		  xfer_ddp;

	/* Associated structures */
	struct fc_rport	  *rport;
	struct fc_seq	  *seq_ptr;

	/* Timeout/error related information */
	struct timer_list timer;
	int	          wait_for_comp;
	u32		  recov_retry;
	struct fc_seq	  *recov_seq;
	struct completion tm_done;
} ____cacheline_aligned_in_smp;

/*
 * Structure and function definitions for managing Fibre Channel Exchanges
 * and Sequences
 *
 * fc_exch holds state for one exchange and links to its active sequence.
 *
 * fc_seq holds the state for an individual sequence.
 */

struct fc_exch_mgr;
struct fc_exch_mgr_anchor;
extern u16 fc_cpu_mask;	/* cpu mask for possible cpus */

/**
 * struct fc_seq - FC sequence
 * @id:       The sequence ID
 * @ssb_stat: Status flags for the sequence status block (SSB)
 * @cnt:      Number of frames sent so far
 * @rec_data: FC-4 value for REC
 */
struct fc_seq {
	u8  id;
	u16 ssb_stat;
	u16 cnt;
	u32 rec_data;
};

#define FC_EX_DONE		(1 << 0) /* ep is completed */
#define FC_EX_RST_CLEANUP	(1 << 1) /* reset is forcing completion */

/**
 * struct fc_exch - Fibre Channel Exchange
 * @em:           Exchange manager
 * @pool:         Exchange pool
 * @state:        The exchange's state
 * @xid:          The exchange ID
 * @ex_list:      Handle used by the EM to track free exchanges
 * @ex_lock:      Lock that protects the exchange
 * @ex_refcnt:    Reference count
 * @timeout_work: Handle for timeout handler
 * @lp:           The local port that this exchange is on
 * @oxid:         Originator's exchange ID
 * @rxid:         Responder's exchange ID
 * @oid:          Originator's FCID
 * @sid:          Source FCID
 * @did:          Destination FCID
 * @esb_stat:     ESB exchange status
 * @r_a_tov:      Resouce allocation time out value (in msecs)
 * @seq_id:       The next sequence ID to use
 * @encaps:       encapsulation information for lower-level driver
 * @f_ctl:        F_CTL flags for the sequence
 * @fh_type:      The frame type
 * @class:        The class of service
 * @seq:          The sequence in use on this exchange
 * @resp_active:  Number of tasks that are concurrently executing @resp().
 * @resp_task:    If @resp_active > 0, either the task executing @resp(), the
 *                task that has been interrupted to execute the soft-IRQ
 *                executing @resp() or NULL if more than one task is executing
 *                @resp concurrently.
 * @resp_wq:      Waitqueue for the tasks waiting on @resp_active.
 * @resp:         Callback for responses on this exchange
 * @destructor:   Called when destroying the exchange
 * @arg:          Passed as a void pointer to the resp() callback
 *
 * Locking notes: The ex_lock protects following items:
 *	state, esb_stat, f_ctl, seq.ssb_stat
 *	seq_id
 *	sequence allocation
 */
struct fc_exch {
	spinlock_t	    ex_lock;
	atomic_t	    ex_refcnt;
	enum fc_class	    class;
	struct fc_exch_mgr  *em;
	struct fc_exch_pool *pool;
	struct list_head    ex_list;
	struct fc_lport	    *lp;
	u32		    esb_stat;
	u8		    state;
	u8		    fh_type;
	u8		    seq_id;
	u8		    encaps;
	u16		    xid;
	u16		    oxid;
	u16		    rxid;
	u32		    oid;
	u32		    sid;
	u32		    did;
	u32		    r_a_tov;
	u32		    f_ctl;
	struct fc_seq       seq;
	int		    resp_active;
	struct task_struct  *resp_task;
	wait_queue_head_t   resp_wq;
	void		    (*resp)(struct fc_seq *, struct fc_frame *, void *);
	void		    *arg;
	void		    (*destructor)(struct fc_seq *, void *);
	struct delayed_work timeout_work;
} ____cacheline_aligned_in_smp;
#define	fc_seq_exch(sp) container_of(sp, struct fc_exch, seq)


struct libfc_function_template {
	/*
	 * Interface to send a FC frame
	 *
	 * STATUS: REQUIRED
	 */
	int (*frame_send)(struct fc_lport *, struct fc_frame *);

	/*
	 * Interface to send ELS/CT frames
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_seq *(*elsct_send)(struct fc_lport *, u32 did,
				     struct fc_frame *, unsigned int op,
				     void (*resp)(struct fc_seq *,
					     struct fc_frame *, void *arg),
				     void *arg, u32 timer_msec);

	/*
	 * Send the FC frame payload using a new exchange and sequence.
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
	struct fc_seq *(*exch_seq_send)(struct fc_lport *, struct fc_frame *,
					void (*resp)(struct fc_seq *,
						     struct fc_frame *,
						     void *),
					void (*destructor)(struct fc_seq *,
							   void *),
					void *, unsigned int timer_msec);

	/*
	 * Sets up the DDP context for a given exchange id on the given
	 * scatterlist if LLD supports DDP for large receive.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*ddp_setup)(struct fc_lport *, u16, struct scatterlist *,
			 unsigned int);
	/*
	 * Completes the DDP transfer and returns the length of data DDPed
	 * for the given exchange id.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*ddp_done)(struct fc_lport *, u16);
	/*
	 * Sets up the DDP context for a given exchange id on the given
	 * scatterlist if LLD supports DDP for target.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*ddp_target)(struct fc_lport *, u16, struct scatterlist *,
			  unsigned int);
	/*
	 * Allow LLD to fill its own Link Error Status Block
	 *
	 * STATUS: OPTIONAL
	 */
	void (*get_lesb)(struct fc_lport *, struct fc_els_lesb *lesb);
	/*
	 * Send a frame using an existing sequence and exchange.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*seq_send)(struct fc_lport *, struct fc_seq *,
			struct fc_frame *);

	/*
	 * Send an ELS response using information from the received frame.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*seq_els_rsp_send)(struct fc_frame *, enum fc_els_cmd,
				 struct fc_seq_els_data *);

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
	int (*seq_exch_abort)(const struct fc_seq *,
			      unsigned int timer_msec);

	/*
	 * Indicate that an exchange/sequence tuple is complete and the memory
	 * allocated for the related objects may be freed.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*exch_done)(struct fc_seq *);

	/*
	 * Start a new sequence on the same exchange/sequence tuple.
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_seq *(*seq_start_next)(struct fc_seq *);

	/*
	 * Set a response handler for the exchange of the sequence.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*seq_set_resp)(struct fc_seq *sp,
			     void (*resp)(struct fc_seq *, struct fc_frame *,
					  void *),
			     void *arg);

	/*
	 * Assign a sequence for an incoming request frame.
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_seq *(*seq_assign)(struct fc_lport *, struct fc_frame *);

	/*
	 * Release the reference on the sequence returned by seq_assign().
	 *
	 * STATUS: OPTIONAL
	 */
	void (*seq_release)(struct fc_seq *);

	/*
	 * Reset an exchange manager, completing all sequences and exchanges.
	 * If s_id is non-zero, reset only exchanges originating from that FID.
	 * If d_id is non-zero, reset only exchanges sending to that FID.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*exch_mgr_reset)(struct fc_lport *, u32 s_id, u32 d_id);

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
	void (*lport_recv)(struct fc_lport *, struct fc_frame *);

	/*
	 * Reset the local port.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*lport_reset)(struct fc_lport *);

	/*
	 * Set the local port FC_ID.
	 *
	 * This may be provided by the LLD to allow it to be
	 * notified when the local port is assigned a FC-ID.
	 *
	 * The frame, if non-NULL, is the incoming frame with the
	 * FLOGI LS_ACC or FLOGI, and may contain the granted MAC
	 * address for the LLD.  The frame pointer may be NULL if
	 * no MAC is associated with this assignment (LOGO or PLOGI).
	 *
	 * If FC_ID is non-zero, r_a_tov and e_d_tov must be valid.
	 *
	 * Note: this is called with the local port mutex held.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*lport_set_port_id)(struct fc_lport *, u32 port_id,
				  struct fc_frame *);

	/*
	 * Create a remote port with a given port ID
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_rport_priv *(*rport_create)(struct fc_lport *, u32);

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
	int (*rport_login)(struct fc_rport_priv *);

	/*
	 * Logoff, and remove the rport from the transport if
	 * it had been added. This will send a LOGO to the target.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*rport_logoff)(struct fc_rport_priv *);

	/*
	 * Receive a request from a remote port.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*rport_recv_req)(struct fc_lport *, struct fc_frame *);

	/*
	 * lookup an rport by it's port ID.
	 *
	 * STATUS: OPTIONAL
	 */
	struct fc_rport_priv *(*rport_lookup)(const struct fc_lport *, u32);

	/*
	 * Destroy an rport after final kref_put().
	 * The argument is a pointer to the kref inside the fc_rport_priv.
	 */
	void (*rport_destroy)(struct kref *);

	/*
	 * Callback routine after the remote port is logged in
	 *
	 * STATUS: OPTIONAL
	 */
	void (*rport_event_callback)(struct fc_lport *,
				     struct fc_rport_priv *,
				     enum fc_rport_event);

	/*
	 * Send a fcp cmd from fsp pkt.
	 * Called with the SCSI host lock unlocked and irqs disabled.
	 *
	 * The resp handler is called when FCP_RSP received.
	 *
	 * STATUS: OPTIONAL
	 */
	int (*fcp_cmd_send)(struct fc_lport *, struct fc_fcp_pkt *,
			    void (*resp)(struct fc_seq *, struct fc_frame *,
					 void *));

	/*
	 * Cleanup the FCP layer, used during link down and reset
	 *
	 * STATUS: OPTIONAL
	 */
	void (*fcp_cleanup)(struct fc_lport *);

	/*
	 * Abort all I/O on a local port
	 *
	 * STATUS: OPTIONAL
	 */
	void (*fcp_abort_io)(struct fc_lport *);

	/*
	 * Receive a request for the discovery layer.
	 *
	 * STATUS: OPTIONAL
	 */
	void (*disc_recv_req)(struct fc_lport *, struct fc_frame *);

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

/**
 * struct fc_disc - Discovery context
 * @retry_count:   Number of retries
 * @pending:       1 if discovery is pending, 0 if not
 * @requested:     1 if discovery has been requested, 0 if not
 * @seq_count:     Number of sequences used for discovery
 * @buf_len:       Length of the discovery buffer
 * @disc_id:       Discovery ID
 * @rports:        List of discovered remote ports
 * @priv:          Private pointer for use by discovery code
 * @disc_mutex:    Mutex that protects the discovery context
 * @partial_buf:   Partial name buffer (if names are returned
 *                 in multiple frames)
 * @disc_work:     handle for delayed work context
 * @disc_callback: Callback routine called when discovery completes
 */
struct fc_disc {
	unsigned char         retry_count;
	unsigned char         pending;
	unsigned char         requested;
	unsigned short        seq_count;
	unsigned char         buf_len;
	u16                   disc_id;

	struct list_head      rports;
	void		      *priv;
	struct mutex	      disc_mutex;
	struct fc_gpn_ft_resp partial_buf;
	struct delayed_work   disc_work;

	void (*disc_callback)(struct fc_lport *,
			      enum fc_disc_event);
};

/*
 * Local port notifier and events.
 */
extern struct blocking_notifier_head fc_lport_notifier_head;
enum fc_lport_event {
	FC_LPORT_EV_ADD,
	FC_LPORT_EV_DEL,
};

/**
 * struct fc_lport - Local port
 * @host:                  The SCSI host associated with a local port
 * @ema_list:              Exchange manager anchor list
 * @dns_rdata:             The directory server remote port
 * @ms_rdata:		   The management server remote port
 * @ptp_rdata:             Point to point remote port
 * @scsi_priv:             FCP layer internal data
 * @disc:                  Discovery context
 * @vports:                Child vports if N_Port
 * @vport:                 Parent vport if VN_Port
 * @tt:                    Libfc function template
 * @link_up:               Link state (1 = link up, 0 = link down)
 * @qfull:                 Queue state (1 queue is full, 0 queue is not full)
 * @state:                 Identifies the state
 * @boot_time:             Timestamp indicating when the local port came online
 * @host_stats:            SCSI host statistics
 * @stats:                 FC local port stats (TODO separate libfc LLD stats)
 * @retry_count:           Number of retries in the current state
 * @port_id:               FC Port ID
 * @wwpn:                  World Wide Port Name
 * @wwnn:                  World Wide Node Name
 * @service_params:        Common service parameters
 * @e_d_tov:               Error detection timeout value
 * @r_a_tov:               Resouce allocation timeout value
 * @rnid_gen:              RNID information
 * @sg_supp:               Indicates if scatter gather is supported
 * @seq_offload:           Indicates if sequence offload is supported
 * @crc_offload:           Indicates if CRC offload is supported
 * @lro_enabled:           Indicates if large receive offload is supported
 * @does_npiv:             Supports multiple vports
 * @npiv_enabled:          Switch/fabric allows NPIV
 * @mfs:                   The maximum Fibre Channel payload size
 * @max_retry_count:       The maximum retry attempts
 * @max_rport_retry_count: The maximum remote port retry attempts
 * @rport_priv_size:       Size needed by driver after struct fc_rport_priv
 * @lro_xid:               The maximum XID for LRO
 * @lso_max:               The maximum large offload send size
 * @fcts:                  FC-4 type mask
 * @lp_mutex:              Mutex to protect the local port
 * @list:                  Linkage on list of vport peers
 * @retry_work:            Handle to local port for delayed retry context
 * @prov:		   Pointers available for use by passive FC-4 providers
 * @lport_list:            Linkage on module-wide list of local ports
 */
struct fc_lport {
	/* Associations */
	struct Scsi_Host	       *host;
	struct list_head	       ema_list;
	struct fc_rport_priv	       *dns_rdata;
	struct fc_rport_priv	       *ms_rdata;
	struct fc_rport_priv	       *ptp_rdata;
	void			       *scsi_priv;
	struct fc_disc                 disc;

	/* Virtual port information */
	struct list_head	       vports;
	struct fc_vport		       *vport;

	/* Operational Information */
	struct libfc_function_template tt;
	u8			       link_up;
	u8			       qfull;
	enum fc_lport_state	       state;
	unsigned long		       boot_time;
	struct fc_host_statistics      host_stats;
	struct fc_stats	__percpu       *stats;
	u8			       retry_count;

	/* Fabric information */
	u32                            port_id;
	u64			       wwpn;
	u64			       wwnn;
	unsigned int		       service_params;
	unsigned int		       e_d_tov;
	unsigned int		       r_a_tov;
	struct fc_els_rnid_gen	       rnid_gen;

	/* Capabilities */
	u32			       sg_supp:1;
	u32			       seq_offload:1;
	u32			       crc_offload:1;
	u32			       lro_enabled:1;
	u32			       does_npiv:1;
	u32			       npiv_enabled:1;
	u32			       point_to_multipoint:1;
	u32			       fdmi_enabled:1;
	u32			       mfs;
	u8			       max_retry_count;
	u8			       max_rport_retry_count;
	u16			       rport_priv_size;
	u16			       link_speed;
	u16			       link_supported_speeds;
	u16			       lro_xid;
	unsigned int		       lso_max;
	struct fc_ns_fts	       fcts;

	/* Miscellaneous */
	struct mutex                   lp_mutex;
	struct list_head               list;
	struct delayed_work	       retry_work;
	void			       *prov[FC_FC4_PROV_SIZE];
	struct list_head               lport_list;
};

/**
 * struct fc4_prov - FC-4 provider registration
 * @prli:               Handler for incoming PRLI
 * @prlo:               Handler for session reset
 * @recv:		Handler for incoming request
 * @module:		Pointer to module.  May be NULL.
 */
struct fc4_prov {
	int (*prli)(struct fc_rport_priv *, u32 spp_len,
		    const struct fc_els_spp *spp_in,
		    struct fc_els_spp *spp_out);
	void (*prlo)(struct fc_rport_priv *);
	void (*recv)(struct fc_lport *, struct fc_frame *);
	struct module *module;
};

/*
 * Register FC-4 provider with libfc.
 */
int fc_fc4_register_provider(enum fc_fh_type type, struct fc4_prov *);
void fc_fc4_deregister_provider(enum fc_fh_type type, struct fc4_prov *);

/*
 * FC_LPORT HELPER FUNCTIONS
 *****************************/

/**
 * fc_lport_test_ready() - Determine if a local port is in the READY state
 * @lport: The local port to test
 */
static inline int fc_lport_test_ready(struct fc_lport *lport)
{
	return lport->state == LPORT_ST_READY;
}

/**
 * fc_set_wwnn() - Set the World Wide Node Name of a local port
 * @lport: The local port whose WWNN is to be set
 * @wwnn:  The new WWNN
 */
static inline void fc_set_wwnn(struct fc_lport *lport, u64 wwnn)
{
	lport->wwnn = wwnn;
}

/**
 * fc_set_wwpn() - Set the World Wide Port Name of a local port
 * @lport: The local port whose WWPN is to be set
 * @wwnn:  The new WWPN
 */
static inline void fc_set_wwpn(struct fc_lport *lport, u64 wwnn)
{
	lport->wwpn = wwnn;
}

/**
 * fc_lport_state_enter() - Change a local port's state
 * @lport: The local port whose state is to change
 * @state: The new state
 */
static inline void fc_lport_state_enter(struct fc_lport *lport,
					enum fc_lport_state state)
{
	if (state != lport->state)
		lport->retry_count = 0;
	lport->state = state;
}

/**
 * fc_lport_init_stats() - Allocate per-CPU statistics for a local port
 * @lport: The local port whose statistics are to be initialized
 */
static inline int fc_lport_init_stats(struct fc_lport *lport)
{
	lport->stats = alloc_percpu(struct fc_stats);
	if (!lport->stats)
		return -ENOMEM;
	return 0;
}

/**
 * fc_lport_free_stats() - Free memory for a local port's statistics
 * @lport: The local port whose statistics are to be freed
 */
static inline void fc_lport_free_stats(struct fc_lport *lport)
{
	free_percpu(lport->stats);
}

/**
 * lport_priv() - Return the private data from a local port
 * @lport: The local port whose private data is to be retreived
 */
static inline void *lport_priv(const struct fc_lport *lport)
{
	return (void *)(lport + 1);
}

/**
 * libfc_host_alloc() - Allocate a Scsi_Host with room for a local port and
 *                      LLD private data
 * @sht:       The SCSI host template
 * @priv_size: Size of private data
 *
 * Returns: libfc lport
 */
static inline struct fc_lport *
libfc_host_alloc(struct scsi_host_template *sht, int priv_size)
{
	struct fc_lport *lport;
	struct Scsi_Host *shost;

	shost = scsi_host_alloc(sht, sizeof(*lport) + priv_size);
	if (!shost)
		return NULL;
	lport = shost_priv(shost);
	lport->host = shost;
	INIT_LIST_HEAD(&lport->ema_list);
	INIT_LIST_HEAD(&lport->vports);
	return lport;
}

/*
 * FC_FCP HELPER FUNCTIONS
 *****************************/
static inline bool fc_fcp_is_read(const struct fc_fcp_pkt *fsp)
{
	if (fsp && fsp->cmd)
		return fsp->cmd->sc_data_direction == DMA_FROM_DEVICE;
	return false;
}

/*
 * LOCAL PORT LAYER
 *****************************/
int fc_lport_init(struct fc_lport *);
int fc_lport_destroy(struct fc_lport *);
int fc_fabric_logoff(struct fc_lport *);
int fc_fabric_login(struct fc_lport *);
void __fc_linkup(struct fc_lport *);
void fc_linkup(struct fc_lport *);
void __fc_linkdown(struct fc_lport *);
void fc_linkdown(struct fc_lport *);
void fc_vport_setlink(struct fc_lport *);
void fc_vports_linkchange(struct fc_lport *);
int fc_lport_config(struct fc_lport *);
int fc_lport_reset(struct fc_lport *);
int fc_set_mfs(struct fc_lport *, u32 mfs);
struct fc_lport *libfc_vport_create(struct fc_vport *, int privsize);
struct fc_lport *fc_vport_id_lookup(struct fc_lport *, u32 port_id);
int fc_lport_bsg_request(struct fc_bsg_job *);
void fc_lport_set_local_id(struct fc_lport *, u32 port_id);
void fc_lport_iterate(void (*func)(struct fc_lport *, void *), void *);

/*
 * REMOTE PORT LAYER
 *****************************/
int fc_rport_init(struct fc_lport *);
void fc_rport_terminate_io(struct fc_rport *);

/*
 * DISCOVERY LAYER
 *****************************/
void fc_disc_init(struct fc_lport *);
void fc_disc_config(struct fc_lport *, void *);

static inline struct fc_lport *fc_disc_lport(struct fc_disc *disc)
{
	return container_of(disc, struct fc_lport, disc);
}

/*
 * FCP LAYER
 *****************************/
int fc_fcp_init(struct fc_lport *);
void fc_fcp_destroy(struct fc_lport *);

/*
 * SCSI INTERACTION LAYER
 *****************************/
int fc_queuecommand(struct Scsi_Host *, struct scsi_cmnd *);
int fc_eh_abort(struct scsi_cmnd *);
int fc_eh_device_reset(struct scsi_cmnd *);
int fc_eh_host_reset(struct scsi_cmnd *);
int fc_slave_alloc(struct scsi_device *);
int fc_change_queue_depth(struct scsi_device *, int qdepth, int reason);
int fc_change_queue_type(struct scsi_device *, int tag_type);

/*
 * ELS/CT interface
 *****************************/
int fc_elsct_init(struct fc_lport *);
struct fc_seq *fc_elsct_send(struct fc_lport *, u32 did,
				    struct fc_frame *,
				    unsigned int op,
				    void (*resp)(struct fc_seq *,
						 struct fc_frame *,
						 void *arg),
				    void *arg, u32 timer_msec);
void fc_lport_flogi_resp(struct fc_seq *, struct fc_frame *, void *);
void fc_lport_logo_resp(struct fc_seq *, struct fc_frame *, void *);
void fc_fill_reply_hdr(struct fc_frame *, const struct fc_frame *,
		       enum fc_rctl, u32 parm_offset);
void fc_fill_hdr(struct fc_frame *, const struct fc_frame *,
		 enum fc_rctl, u32 f_ctl, u16 seq_cnt, u32 parm_offset);


/*
 * EXCHANGE MANAGER LAYER
 *****************************/
int fc_exch_init(struct fc_lport *);
void fc_exch_update_stats(struct fc_lport *lport);
struct fc_exch_mgr_anchor *fc_exch_mgr_add(struct fc_lport *,
					   struct fc_exch_mgr *,
					   bool (*match)(struct fc_frame *));
void fc_exch_mgr_del(struct fc_exch_mgr_anchor *);
int fc_exch_mgr_list_clone(struct fc_lport *src, struct fc_lport *dst);
struct fc_exch_mgr *fc_exch_mgr_alloc(struct fc_lport *, enum fc_class class,
				      u16 min_xid, u16 max_xid,
				      bool (*match)(struct fc_frame *));
void fc_exch_mgr_free(struct fc_lport *);
void fc_exch_recv(struct fc_lport *, struct fc_frame *);
void fc_exch_mgr_reset(struct fc_lport *, u32 s_id, u32 d_id);

/*
 * Functions for fc_functions_template
 */
void fc_get_host_speed(struct Scsi_Host *);
void fc_get_host_port_state(struct Scsi_Host *);
void fc_set_rport_loss_tmo(struct fc_rport *, u32 timeout);
struct fc_host_statistics *fc_get_host_stats(struct Scsi_Host *);

#endif /* _LIBFC_H_ */
