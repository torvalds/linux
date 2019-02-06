#ifndef _SOCKET_KOBJECT_H
#define _SOCKET_KOBJECT_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <uapi/linux/net.h>
#include <net/sock.h>
#include <linux/rcupdate.h>
#include <linux/medusa/l1/socket.h>
#include "../../fs/internal.h"
#include <linux/medusa/l3/registry.h>

struct socket_kobject {
	dev_t dev;
	unsigned long ino;

	int type;
	int family;
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
