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
#include "../../../../security/medusa/l2/kobject_socket.h"
#include <linux/types.h>

#pragma GCC optimize ("Og")

/**
 * struct medusa_l1_socket_s - additional security struct for socket objects
 *
 * @MEDUSA_OBJECT_VARS - members used in Medusa VS access evaluation process
 */
struct medusa_l1_socket_s {
	MEDUSA_OBJECT_VARS;
	int addrlen;
	void *address;
};

struct med_inet_addr_i {
	__be16 port;
	__be32 *addrdata;
};

struct med_unix_addr_i {
	char *addrdata;
};

extern medusa_answer_t medusa_socket_create(int family, int type, int protocol);
extern medusa_answer_t medusa_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen);
extern medusa_answer_t medusa_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen);
extern medusa_answer_t medusa_socket_listen(struct socket *sock, int backlog);
extern medusa_answer_t medusa_socket_accept(struct socket *sock, struct socket *newsock);
extern medusa_answer_t medusa_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size);
extern medusa_answer_t medusa_socket_recvmsg(struct socket *sock, struct msghdr *msg, int size, int flags);
/*
 * The following routine makes a support for many of access types,
 * and it is used both in L1 and L2 code. It is defined in
 * l2/evtype_getsocket.c.
 */
extern medusa_answer_t socket_kobj_validate(struct socket *sock);

#endif
