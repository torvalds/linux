#ifndef NPPPD_RADIUS_H
#define NPPPD_RADIUS_H 1

#include <sys/tree.h>
#include <netinet/in.h>
#include <event.h>

struct npppd_radius_dae_listen {
	int					 sock;
	struct event				 evsock;
	union {
		struct sockaddr_in		 sin4;
		struct sockaddr_in6		 sin6;
	}					 addr;
	npppd					*pppd;
	TAILQ_ENTRY(npppd_radius_dae_listen)	 entry;
};

TAILQ_HEAD(npppd_radius_dae_listens, npppd_radius_dae_listen);

#ifdef __cplusplus
extern "C" {
#endif

void	 ppp_proccess_radius_framed_ip(npppd_ppp *, RADIUS_PACKET *);
int	 ppp_set_radius_attrs_for_authreq(npppd_ppp *, radius_req_setting *,
	    RADIUS_PACKET *);
void	 npppd_ppp_radius_acct_start(npppd *, npppd_ppp *);
void	 npppd_ppp_radius_acct_stop(npppd *, npppd_ppp *);
void	 radius_acct_on(npppd *, radius_req_setting *);
void	 npppd_radius_dae_init(npppd *);
void	 npppd_radius_dae_fini(npppd *);

#ifdef __cplusplus
}
#endif
#endif
