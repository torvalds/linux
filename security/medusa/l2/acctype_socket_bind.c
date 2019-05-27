#include <net/ipv6.h>
#include <uapi/linux/un.h>
#include "kobject_process.h"
#include "kobject_socket.h"
#include "kobject_file.h"

struct socket_bind_access {
	MEDUSA_ACCESS_HEADER;
	sa_family_t family;
	int addrlen;
	MED_ADDRESS address;
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

medusa_answer_t medusa_socket_bind_security(struct socket *sock, struct socket_bind_access *access)
{
	struct process_kobject process;
	struct socket_kobject sock_kobj;
	struct medusa_l1_socket_s *sk_sec = &sock_security(sock->sk);
	medusa_answer_t retval;

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
		retval = MED_DECIDE(socket_bind_access, access, &process, &sock_kobj);
		if(retval != MED_ERR) {
			switch(sock->sk->sk_family) {
				case AF_INET:
					sk_sec->address.inet_i.port = access->address.inet_i.port;
					memcpy(sk_sec->address.inet_i.addrdata, access->address.inet_i.addrdata, 4);
					break;
				case AF_INET6:
					sk_sec->address.inet6_i.port = access->address.inet6_i.port;
					memcpy(sk_sec->address.inet6_i.addrdata, access->address.inet6_i.addrdata, 16);
					break;
				case AF_UNIX:
					memcpy(sk_sec->address.unix_i.addrdata, access->address.unix_i.addrdata, UNIX_PATH_MAX);
					break;
				default:
					break;
			}
			sk_sec->addrlen = access->addrlen;
		}
		return retval;
	}
	return MED_YES;
}

medusa_answer_t medusa_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	const int family = sock->sk->sk_family;
	struct socket_bind_access access;

	access.family = family;
	access.addrlen = addrlen;

	switch(family) {
		case AF_INET:
			if (addrlen < sizeof(struct sockaddr_in))
				return -EINVAL;
			access.address.inet_i.port = ((struct sockaddr_in *) address)->sin_port;
			memcpy(access.address.inet_i.addrdata, (__be32 *)&((struct sockaddr_in *) address)->sin_addr, 4);
			break;
		case AF_INET6:
			if (addrlen < SIN6_LEN_RFC2133)
				return -EINVAL;
			access.address.inet6_i.port = ((struct sockaddr_in6 *) address)->sin6_port;
			memcpy(access.address.inet6_i.addrdata, (__be32 *)((struct sockaddr_in6 *)address)->sin6_addr.s6_addr, 16);
			break;
		case AF_UNIX:
			memcpy(access.address.unix_i.addrdata, ((struct sockaddr_un *) address)->sun_path, UNIX_PATH_MAX);
			break;
		default:
			return MED_YES;
	}
	return medusa_socket_bind_security(sock, &access);
}

__initcall(socket_bind_access_init);
