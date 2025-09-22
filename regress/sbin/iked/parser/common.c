/*	$OpenBSD: common.c,v 1.13 2022/12/03 22:34:35 tobhe Exp $ */
/*
 * A bunch of stub functions so we can compile and link ikev2_pld.c
 * in a standalone program for testing purposes.
 *
 * Placed in the public domain
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <event.h>
#include <limits.h>

#include "iked.h"
#include "types.h"
#include "test_helper.h"

int	 eap_parse(struct iked *, const struct iked_sa *,
	    struct iked_message *, void *, int);
int	 ikev2_msg_frompeer(struct iked_message *);
int	 ikev2_send_ike_e(struct iked *, struct iked_sa *, struct ibuf *,
	    uint8_t, uint8_t, int);
void	 ikev2_ikesa_recv_delete(struct iked *, struct iked_sa *);
struct iked_childsa *
	 childsa_lookup(struct iked_sa *, uint64_t, uint8_t);
int	  ikev2_childsa_delete(struct iked *, struct iked_sa *,
	    uint8_t, uint64_t, uint64_t *, int);
int	 sa_stateok(const struct iked_sa *, int);
void	 sa_state(struct iked *, struct iked_sa *, int);
void	 ikev2_disable_rekeying(struct iked *, struct iked_sa *);
void	 ikev2_init_ike_sa(struct iked *, void *);
struct dh_group *
	 group_get(uint32_t);
void	 timer_set(struct iked *, struct iked_timer *,
	     void (*)(struct iked *, void *), void *);
void	 timer_add(struct iked *, struct iked_timer *, int);
void	 timer_del(struct iked *, struct iked_timer *);
ssize_t	 ikev2_nat_detection(struct iked *, struct iked_message *,
	     void *, size_t, u_int, int);
int	 ca_setreq(struct iked *, struct iked_sa *, struct iked_static_id *,
	     uint8_t, uint8_t, uint8_t *, size_t, enum privsep_procid);
int	 ikev2_print_id(struct iked_id *, char *, size_t);
int	 config_add_transform(struct iked_proposal *, u_int, u_int, u_int,
	     u_int);
struct iked_proposal *
	 config_add_proposal(struct iked_proposals *, u_int, u_int);
void	 config_free_proposal(struct iked_proposals *, struct iked_proposal *);
int	 ikev2_send_informational(struct iked *, struct iked_message *);
struct ibuf *
	 ikev2_msg_decrypt(struct iked *, struct iked_sa *, struct ibuf *,
	     struct ibuf *);

int
eap_parse(struct iked *env, const struct iked_sa *sa, struct iked_message *msg,
    void *data, int response)
{
	return (0);
}

int
ikev2_msg_frompeer(struct iked_message *msg)
{
	return (0);
}

int
ikev2_send_ike_e(struct iked *env, struct iked_sa *sa, struct ibuf *buf,
    uint8_t firstpayload, uint8_t exchange, int response)
{
	return (0);
}

void
ikev2_ikesa_recv_delete(struct iked *env, struct iked_sa *sa)
{
}

const char *
ikev2_ikesa_info(uint64_t spi, const char *msg)
{
	return "";
}

struct iked_childsa *
childsa_lookup(struct iked_sa *a, uint64_t b, uint8_t c)
{
	return (NULL);
}

int
ikev2_childsa_delete(struct iked *a, struct iked_sa *b, uint8_t c,
    uint64_t d, uint64_t *e , int f)
{
	return (0);
}

int
sa_stateok(const struct iked_sa *a, int b)
{
	return (0);
}

void
sa_state(struct iked * a, struct iked_sa *b, int c)
{
}

void
ikev2_disable_rekeying(struct iked *a, struct iked_sa *b)
{
}

void
ikev2_init_ike_sa(struct iked *a, void *b)
{
}

const struct group_id *
group_getid(uint32_t id)
{
	return (NULL);
}

void
timer_set(struct iked *env, struct iked_timer *tmr,
    void (*cb)(struct iked *, void *), void *arg)
{
}

void
timer_add(struct iked *env, struct iked_timer *tmr, int timeout)
{
}

void
timer_del(struct iked *env, struct iked_timer *tmr)
{
}

ssize_t
ikev2_nat_detection(struct iked *env, struct iked_message *msg,
    void *ptr, size_t len, u_int type, int frompeer)
{
	return (0);
}

int
ca_setreq(struct iked *env, struct iked_sa *sh, struct iked_static_id *localid,
    uint8_t type, uint8_t more, uint8_t *data, size_t len,
    enum privsep_procid procid)
{
	return (0);
}

int
ikev2_print_id(struct iked_id *id, char *idstr, size_t idstrlen)
{
	return (0);
}

int
config_add_transform(struct iked_proposal *prop, u_int type,
    u_int id, u_int length, u_int keylength)
{
	return (0);
}

struct iked_proposal *
config_add_proposal(struct iked_proposals *head, u_int id, u_int proto)
{
	return (NULL);
}

void
config_free_proposal(struct iked_proposals *head, struct iked_proposal *prop)
{
	return;
}

void config_free_fragments(struct iked_frag *frag)
{
	return;
}

int
ikev2_send_informational(struct iked *env, struct iked_message *msg)
{
	return (0);
}

struct ibuf *
ikev2_msg_decrypt(struct iked *env, struct iked_sa *sa,
    struct ibuf *msg, struct ibuf *src)
{
	ASSERT_PTR_NE(src, NULL);

	/*
	 * Free src as caller uses ikev2_msg_decrypt() like this:
	 * src = ikev2_msg_decrypt(..., src);
	 */
	ibuf_free(src);	
	return (NULL);
}

void
ikev2_ike_sa_setreason(struct iked_sa *sa, char *r)
{
}

void
ikev2_msg_dispose(struct iked *env, struct iked_msgqueue *queue,
    struct iked_msg_retransmit *mr)
{
}

struct iked_msg_retransmit *
ikev2_msg_lookup(struct iked *env, struct iked_msgqueue *queue,
    struct iked_message *msg, uint8_t exchange)
{
	return NULL;
}
