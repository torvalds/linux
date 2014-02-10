#ifndef SCSI_TRANSPORT_SRP_H
#define SCSI_TRANSPORT_SRP_H

#include <linux/transport_class.h>
#include <linux/types.h>
#include <linux/mutex.h>

#define SRP_RPORT_ROLE_INITIATOR 0
#define SRP_RPORT_ROLE_TARGET 1

struct srp_rport_identifiers {
	u8 port_id[16];
	u8 roles;
};

/**
 * enum srp_rport_state - SRP transport layer state
 * @SRP_RPORT_RUNNING:   Transport layer operational.
 * @SRP_RPORT_BLOCKED:   Transport layer not operational; fast I/O fail timer
 *                       is running and I/O has been blocked.
 * @SRP_RPORT_FAIL_FAST: Fast I/O fail timer has expired; fail I/O fast.
 * @SRP_RPORT_LOST:      Port is being removed.
 */
enum srp_rport_state {
	SRP_RPORT_RUNNING,
	SRP_RPORT_BLOCKED,
	SRP_RPORT_FAIL_FAST,
	SRP_RPORT_LOST,
};

/**
 * struct srp_rport - SRP initiator or target port
 *
 * Fields that are relevant for SRP initiator and SRP target drivers:
 * @dev:               Device associated with this rport.
 * @port_id:           16-byte port identifier.
 * @roles:             Role of this port - initiator or target.
 *
 * Fields that are only relevant for SRP initiator drivers:
 * @lld_data:          LLD private data.
 * @mutex:             Protects against concurrent rport reconnect /
 *                     fast_io_fail / dev_loss_tmo activity.
 * @state:             rport state.
 * @deleted:           Whether or not srp_rport_del() has already been invoked.
 * @reconnect_delay:   Reconnect delay in seconds.
 * @failed_reconnects: Number of failed reconnect attempts.
 * @reconnect_work:    Work structure used for scheduling reconnect attempts.
 * @fast_io_fail_tmo:  Fast I/O fail timeout in seconds.
 * @dev_loss_tmo:      Device loss timeout in seconds.
 * @fast_io_fail_work: Work structure used for scheduling fast I/O fail work.
 * @dev_loss_work:     Work structure used for scheduling device loss work.
 */
struct srp_rport {
	/* for initiator and target drivers */

	struct device dev;

	u8 port_id[16];
	u8 roles;

	/* for initiator drivers */

	void			*lld_data;

	struct mutex		mutex;
	enum srp_rport_state	state;
	int			reconnect_delay;
	int			failed_reconnects;
	struct delayed_work	reconnect_work;
	int			fast_io_fail_tmo;
	int			dev_loss_tmo;
	struct delayed_work	fast_io_fail_work;
	struct delayed_work	dev_loss_work;
};

/**
 * struct srp_function_template
 *
 * Fields that are only relevant for SRP initiator drivers:
 * @has_rport_state: Whether or not to create the state, fast_io_fail_tmo and
 *     dev_loss_tmo sysfs attribute for an rport.
 * @reset_timer_if_blocked: Whether or srp_timed_out() should reset the command
 *     timer if the device on which it has been queued is blocked.
 * @reconnect_delay: If not NULL, points to the default reconnect_delay value.
 * @fast_io_fail_tmo: If not NULL, points to the default fast_io_fail_tmo value.
 * @dev_loss_tmo: If not NULL, points to the default dev_loss_tmo value.
 * @reconnect: Callback function for reconnecting to the target. See also
 *     srp_reconnect_rport().
 * @terminate_rport_io: Callback function for terminating all outstanding I/O
 *     requests for an rport.
 * @rport_delete: Callback function that deletes an rport.
 *
 * Fields that are only relevant for SRP target drivers:
 * @tsk_mgmt_response: Callback function for sending a task management response.
 * @it_nexus_response: Callback function for processing an IT nexus response.
 */
struct srp_function_template {
	/* for initiator drivers */
	bool has_rport_state;
	bool reset_timer_if_blocked;
	int *reconnect_delay;
	int *fast_io_fail_tmo;
	int *dev_loss_tmo;
	int (*reconnect)(struct srp_rport *rport);
	void (*terminate_rport_io)(struct srp_rport *rport);
	void (*rport_delete)(struct srp_rport *rport);
	/* for target drivers */
	int (* tsk_mgmt_response)(struct Scsi_Host *, u64, u64, int);
	int (* it_nexus_response)(struct Scsi_Host *, u64, int);
};

extern struct scsi_transport_template *
srp_attach_transport(struct srp_function_template *);
extern void srp_release_transport(struct scsi_transport_template *);

extern void srp_rport_get(struct srp_rport *rport);
extern void srp_rport_put(struct srp_rport *rport);
extern struct srp_rport *srp_rport_add(struct Scsi_Host *,
				       struct srp_rport_identifiers *);
extern void srp_rport_del(struct srp_rport *);
extern int srp_tmo_valid(int reconnect_delay, int fast_io_fail_tmo,
			 int dev_loss_tmo);
extern int srp_reconnect_rport(struct srp_rport *rport);
extern void srp_start_tl_fail_timers(struct srp_rport *rport);
extern void srp_remove_host(struct Scsi_Host *);
extern void srp_stop_rport_timers(struct srp_rport *rport);

/**
 * srp_chkready() - evaluate the transport layer state before I/O
 * @rport: SRP target port pointer.
 *
 * Returns a SCSI result code that can be returned by the LLD queuecommand()
 * implementation. The role of this function is similar to that of
 * fc_remote_port_chkready().
 */
static inline int srp_chkready(struct srp_rport *rport)
{
	switch (rport->state) {
	case SRP_RPORT_RUNNING:
	case SRP_RPORT_BLOCKED:
	default:
		return 0;
	case SRP_RPORT_FAIL_FAST:
		return DID_TRANSPORT_FAILFAST << 16;
	case SRP_RPORT_LOST:
		return DID_NO_CONNECT << 16;
	}
}

#endif
