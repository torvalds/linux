/*	$OpenBSD: netdb.c,v 1.2 2011/05/01 04:27:07 guenther Exp $	*/

/*
 * Public domain, 2004, Otto Moerbeek <otto@drijf.net>
 */

#include <err.h>
#include <netdb.h>
#include <stdarg.h>
#include <string.h>

int ret = 0;

void
checkp(int n, struct protoent *p, int proto, const char *name, ...)
{
	va_list va;
	char *a;
	int i;

	if (p == NULL) {
		warnx("%d proto struct is NULL", n);
		ret = 1;
		return;
	}
	if (p->p_proto != proto) {
		warnx("%d proto num mismatch %d %d", n, p->p_proto, proto);
		ret = 1;
	}
	if (strcmp(p->p_name, name) != 0) {
		warnx("%d proto name mismatch %s %s", n, p->p_name, name);
		ret = 1;
	}
	va_start(va, name);
	a = va_arg(va, char *);
	i = 0;
	while (a != NULL) {
		if (strcmp(p->p_aliases[i], a) != 0) {
			warnx("%d proto alias mismatch %s %s", n,
			    p->p_aliases[i], a);
			ret = 1;
		}
		i++;
		a = va_arg(va, char *);
	}
	if (p->p_aliases[i] != NULL) {
			warnx("%d proto alias list does not end with NULL", n);
			ret = 1;
	}
	va_end(va);
}


void
checks(int n, struct servent *s, int port, const char *proto,
    const char *name, ...)
{
	va_list va;
	char *a;
	int i;

	if (s == NULL) {
		warnx("%d serv struct is NULL", n);
		ret = 1;
		return;
	}
	if (s->s_port != ntohs(port)) {
		warnx("%d port num mismatch %d %d", n, s->s_port, port);	
		ret = 1;
	}
	if (strcmp(s->s_name, name) != 0) {
		warnx("%d serv name mismatch %s %s", n, s->s_name, name);
		ret = 1;
	}
	if (strcmp(s->s_proto, proto) != 0) {
		warnx("%d serv proto mismatch %s %s", n, s->s_proto, proto);
		ret = 1;
	}
	va_start(va, name);
	a = va_arg(va, char *);
	i = 0;
	while (a != NULL) {
		if (strcmp(s->s_aliases[i], a) != 0) {
			warnx("%d serv alias mismatch %s %s", n,
			    s->s_aliases[i], a);
			ret = 1;
		}
		i++;
		a = va_arg(va, char *);
	}
	if (s->s_aliases[i] != NULL) {
			warnx("%d serv alias list does not end with NULL", n);
			ret = 1;
	}
	va_end(va);
}

int
main()
{
	struct protoent *p;
	struct protoent protoent;
	struct protoent_data protoent_data;
	struct servent *s;
	struct servent servent;
	struct servent_data servent_data;
	int r;

	p = getprotobynumber(35);
	checkp(1, p, 35, "idpr", "IDPR", (char *)NULL);

	p = getprotobyname("igp");
	checkp(2, p, 9, "igp", "IGP", (char *) NULL);

	p = getprotobyname("RDP");
	checkp(3, p, 27, "rdp", "RDP", (char *) NULL);

	p = getprotobyname("vrrp");
	checkp(4, p, 112, "carp", "CARP", "vrrp", (char *) NULL);

	p = getprotobyname("nonexistent");
	if (p != NULL)
		err(1, "nonexistent proto found");

	memset(&protoent_data, 0, sizeof(protoent_data));
	r = getprotobynumber_r(35, &protoent, &protoent_data);
	if (r != 0) 
		err(1, "proto not found");

	checkp(5, &protoent, 35, "idpr", "IDPR", (char *)NULL);

	r = getprotobyname_r("vrrp", &protoent, &protoent_data);
	if (r != 0) 
		err(1, "proto not found");
	checkp(6, &protoent, 112, "carp", "CARP", "vrrp", (char *) NULL);

	r = getprotobyname_r("nonexistent", &protoent, &protoent_data);
	if (r != -1)
		err(1, "nonexistent proto found");

	r = getprotobyname_r("", &protoent, &protoent_data);
	if (r != -1)
		err(1, "nonexistent proto found");


	r = getprotobynumber_r(256, &protoent, &protoent_data);
	if (r != -1)
		err(1, "nonexistent proto found");

	s = getservbyname("zip", NULL);
	checks(7, s, 6, "ddp", "zip", (char *)NULL);

	s = getservbyname("nicname", "tcp");
	checks(8, s, 43, "tcp", "whois", "nicname", (char *)NULL);

	s = getservbyport(htons(42), "tcp");
	checks(9, s, 42, "tcp", "nameserver", "name", (char *)NULL);

	memset(&servent_data, 0, sizeof(servent_data));
	r = getservbyname_r("zip", "ddp", &servent, &servent_data);
	if (r != 0) 
		err(1, "servent not found");
	checks(10, &servent, 6, "ddp", "zip", (char *)NULL);

	r = getservbyport_r(htons(520), NULL, &servent, &servent_data);
	if (r != 0) 
		err(1, "servent not found");
	checks(11, &servent, 520, "udp", "route", "router", "routed", (char *)NULL);

	r = getservbyname_r("nonexistent", NULL, &servent, &servent_data);
	if (r != -1) 
		err(1, "nonexiststent servent found");

	r = getservbyport_r(htons(50000), NULL, &servent, &servent_data);
	if (r != -1) 
		err(1, "nonexistent servent found");

	r = getservbyport_r(htons(520), "tcp", &servent, &servent_data);
	if (r != -1) 
		err(1, "nonexistent servent found");

	return ret;
}
