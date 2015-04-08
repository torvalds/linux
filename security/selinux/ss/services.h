/*
 * Implementation of the security services.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _SS_SERVICES_H_
#define _SS_SERVICES_H_

#include "policydb.h"
#include "sidtab.h"

extern struct policydb policydb;

void services_compute_operation_type(struct operation *ops,
				struct avtab_node *node);

void services_compute_operation_num(struct operation_decision *od,
					struct avtab_node *node);

#endif	/* _SS_SERVICES_H_ */

