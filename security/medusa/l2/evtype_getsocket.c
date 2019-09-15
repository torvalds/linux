#include <linux/medusa/l1/socket.h>
#include "kobject_socket.h"

struct socket_event {
	MEDUSA_ACCESS_HEADER;
};

MED_ATTRS(socket_event) {
	MED_ATTR_END
};

MED_EVTYPE(socket_event, "get_socket", socket_kobject, "socket", socket_kobject, "socket");

int __init socket_evtype_init(void) {
	MED_REGISTER_EVTYPE(socket_event, MEDUSA_EVTYPE_NOTTRIGGERED);
	return 0;
}

medusa_answer_t socket_kobj_validate(struct socket *sock) {
	struct socket_event event;
	struct socket_kobject sock_kobj;
	struct medusa_l1_socket_s *sk_sec;

	if (!sock->sk) {
		return MED_YES;
	}

	sk_sec = &sock_security(sock->sk);
	INIT_MEDUSA_OBJECT_VARS(sk_sec);
	socket_kern2kobj(&sock_kobj, sock);

	if (MED_DECIDE(socket_event, &event, &sock_kobj, &sock_kobj) == MED_ERR)
		return MED_ERR;

	return MED_YES;
}

__initcall(socket_evtype_init);
