#ifndef SCSI_TRANSPORT_SRP_H
#define SCSI_TRANSPORT_SRP_H

#include <linux/transport_class.h>
#include <linux/types.h>
#include <linux/mutex.h>

struct srp_rport_identifiers {
	u8 port_id[16];
};

struct srp_rport {
	struct device dev;

	u8 port_id[16];
};

struct srp_function_template {
	/* later */
};

extern struct scsi_transport_template *
srp_attach_transport(struct srp_function_template *);
extern void srp_release_transport(struct scsi_transport_template *);

extern struct srp_rport *srp_rport_add(struct Scsi_Host *,
				       struct srp_rport_identifiers *);
extern void srp_rport_del(struct srp_rport *);

extern void srp_remove_host(struct Scsi_Host *);

#endif
