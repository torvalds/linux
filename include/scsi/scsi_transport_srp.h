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

struct srp_rport {
	struct device dev;

	u8 port_id[16];
	u8 roles;
};

struct srp_function_template {
	/* for target drivers */
	int (* tsk_mgmt_response)(struct Scsi_Host *, u64, u64, int);
	int (* it_nexus_response)(struct Scsi_Host *, u64, int);
};

extern struct scsi_transport_template *
srp_attach_transport(struct srp_function_template *);
extern void srp_release_transport(struct scsi_transport_template *);

extern struct srp_rport *srp_rport_add(struct Scsi_Host *,
				       struct srp_rport_identifiers *);
extern void srp_rport_del(struct srp_rport *);

extern void srp_remove_host(struct Scsi_Host *);

#endif
