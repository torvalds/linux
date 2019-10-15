#ifndef _SOCKET_KOBJECT_H
#define _SOCKET_KOBJECT_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <uapi/linux/net.h>
#include <uapi/linux/un.h>
#include <net/sock.h>
#include <linux/rcupdate.h>
#include <linux/medusa/l1/socket.h>
#include <linux/medusa/l3/registry.h>
#include "../../fs/internal.h" // For user_get_super()

#define sock_security(sk) (*(struct medusa_l1_socket_s*)(sk->sk_security))

struct med_inet6_addr_i {
	__be16 port;
	__be32 addrdata[16];
};

struct med_inet_addr_i {
	__be16 port;
	__be32 addrdata[4];
};

struct med_unix_addr_i {
	char addrdata[UNIX_PATH_MAX];
};

union MED_ADDRESS {
	struct med_inet6_addr_i inet6_i;
	struct med_inet_addr_i inet_i;
	struct med_unix_addr_i unix_i;
};
typedef union MED_ADDRESS MED_ADDRESS;

/**
 * struct medusa_l1_socket_s - additional security struct for socket objects
 *
 * @MEDUSA_OBJECT_VARS - members used in Medusa VS access evaluation process
 */
struct medusa_l1_socket_s {
	MEDUSA_OBJECT_VARS;
	int addrlen;
	MED_ADDRESS address;
};

struct socket_kobject {
	dev_t dev;
	unsigned long ino;

	int type;
	int family;
	int addrlen;
	union MED_ADDRESS address;
	kuid_t uid;

	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(socket_kobject);

/* the conversion routines */
int socket_kobj2kern(struct socket_kobject * sock_kobj, struct socket * sock);
int socket_kern2kobj(struct socket_kobject * sock_kobj, struct socket * sock);

struct medusa_kobject_s *socket_fetch(struct medusa_kobject_s *kobj);
medusa_answer_t socket_update(struct medusa_kobject_s *kobj);

#endif
