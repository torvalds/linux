/* medusa/l1/socket.h, (C) 2019 Michal Zelencik
 *
 * sock struct extension: this structure is appended to in-kernel data,
 * and we define it separately just to make l1 code shorter.
 *
 * for another data structure - kobject, describing socket for upper layers - 
 * see security/medusa/l2/kobject_socket.[ch].
 */

#ifndef _MEDUSA_L1_SOCKET_H
#define _MEDUSA_L1_SOCKET_H

#include <linux/medusa/l3/model.h>
#include <linux/medusa/l3/constants.h>

#pragma GCC optimize ("Og")

/**
 * struct medusa_l1_socket_s - additional security struct for socket objects
 *
 * @MEDUSA_OBJECT_VARS - members used in Medusa VS access evaluation process
 */
struct medusa_l1_socket_s {
	MEDUSA_OBJECT_VARS;
};

#endif
