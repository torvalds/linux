#include "kobject_process.h"
#include "kobject_socket.h"
#include "kobject_file.h"

struct socket_accept_access {
	MEDUSA_ACCESS_HEADER;

};

MED_ATTRS(socket_accept_access) {
	MED_ATTR_END
};

MED_ACCTYPE(socket_accept_access, "socket_accept_access", process_kobject, "process", socket_kobject, "socket");

int __init socket_accept_access_init(void) {
	MED_REGISTER_ACCTYPE(socket_accept_access, MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_socket_accept(struct socket *sock, struct socket *newsock)
{
	struct socket_accept_access access;
	struct process_kobject process;
	struct socket_kobject sock_kobj;

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		return MED_YES;
	if (!MED_MAGIC_VALID(&sock_security(sock->sk)) && socket_kobj_validate(sock) <= 0)
		return MED_YES;

	if (!VS_INTERSECT(VSS(&task_security(current)),VS(&sock_security(sock->sk))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(&sock_security(sock->sk))))
		return MED_ERR;

	if (MEDUSA_MONITORED_ACCESS_S(socket_accept_access, &task_security(current))) {
		socket_kern2kobj(&sock_kobj, sock);
		process_kern2kobj(&process, current);

		return MED_DECIDE(socket_accept_access, &access, &process, &sock_kobj);
	}
	return MED_YES;
}

__initcall(socket_accept_access_init);
