#include <net/ipv6.h>
#include <uapi/linux/un.h>
#include "kobject_process.h"
#include "kobject_socket.h"
#include "kobject_file.h"

struct socket_bind_access {
	MEDUSA_ACCESS_HEADER;
	sa_family_t family;
	int addrlen;
	void *address;
};

MED_ATTRS(socket_bind_access) {
	MED_ATTR_RO (socket_bind_access, family, "family", MED_UNSIGNED),
	MED_ATTR (socket_bind_access, address, "address", MED_BYTES),
	MED_ATTR_RO (socket_bind_access, addrlen, "addrlen", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(socket_bind_access, "socket_bind_access", process_kobject, "process", socket_kobject, "socket");

int __init socket_bind_access_init(void) {
	MED_REGISTER_ACCTYPE(socket_bind_access, MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_socket_bind_security(struct socket *sock)
{
	struct socket_bind_access access;
	struct process_kobject process;
	struct socket_kobject sock_kobj;
	struct medusa_l1_socket_s *sk_sec = &sock_security(sock->sk);

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		return MED_YES;
	if (!MED_MAGIC_VALID(&sock_security(sock->sk)) && socket_kobj_validate(sock) <= 0)
		return MED_YES;

	if (!VS_INTERSECT(VSS(&task_security(current)),VS(&sock_security(sock->sk))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(&sock_security(sock->sk))))
		return MED_ERR;

	if (MEDUSA_MONITORED_ACCESS_S(socket_bind_access, &task_security(current))) {
		process_kern2kobj(&process, current);
		socket_kern2kobj(&sock_kobj, sock);
		access.family = sock->sk->sk_family;
		access.addrlen = sk_sec->addrlen;
		access.address = sk_sec->address;

		return MED_DECIDE(socket_bind_access, &access, &process, &sock_kobj);
	}
	return MED_YES;
}

medusa_answer_t medusa_socket_bind_inet(struct socket *sock, struct sockaddr *address, int addrlen)
{
	struct med_inet_addr_i *addr = (struct med_inet_addr_i*)kmalloc(sizeof(struct med_inet_addr_i), GFP_KERNEL);
	struct medusa_l1_socket_s *sk_sec = &sock_security(sock->sk);

	switch(address->sa_family) {
		case AF_INET6:
			if (addrlen < SIN6_LEN_RFC2133)
				return -EINVAL;
			addr->port = ((struct sockaddr_in6 *) address)->sin6_port;
			addr->addrdata = (__be32 *)((struct sockaddr_in6 *)address)->sin6_addr.s6_addr;
			break;
		case AF_INET:
			if (addrlen < sizeof(struct sockaddr_in))
				return -EINVAL;
			addr->port = ((struct sockaddr_in *) address)->sin_port;
			addr->addrdata = (__be32 *)&((struct sockaddr_in *) address)->sin_addr;
			break;
		default:
			return MED_ERR;
	}
	sk_sec->address = addr;
	sk_sec->addrlen = addrlen;

	return medusa_socket_bind_security(sock);
}

medusa_answer_t medusa_socket_bind_unix(struct socket *sock, struct sockaddr *address, int addrlen)
{
	struct med_unix_addr_i *addr = (struct med_unix_addr_i*)kmalloc(sizeof(struct med_unix_addr_i), GFP_KERNEL);
	struct medusa_l1_socket_s *sk_sec = &sock_security(sock->sk);

	addr->addrdata = ((struct sockaddr_un *) address)->sun_path;
	sk_sec->address = addr;
	sk_sec->addrlen = addrlen;

	return medusa_socket_bind_security(sock);
}

medusa_answer_t medusa_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	const int family = sock->sk->sk_family;

	switch(family) {
		case AF_INET:
		case AF_INET6:
			return medusa_socket_bind_inet(sock, address, addrlen);
			break;
		case AF_UNIX:
			return medusa_socket_bind_unix(sock, address, addrlen);
			break;
		default:
			break;
	}
	return MED_YES;
}

__initcall(socket_bind_access_init);
