// SPDX-License-Identifier: GPL-2.0
/*
 * Test for sockmap/sockhash redirection.
 *
 * BPF_MAP_TYPE_SOCKMAP
 * BPF_MAP_TYPE_SOCKHASH
 *	x
 * sk_msg-to-egress
 * sk_msg-to-ingress
 * sk_skb-to-egress
 * sk_skb-to-ingress
 *	x
 * AF_INET, SOCK_STREAM
 * AF_INET6, SOCK_STREAM
 * AF_INET, SOCK_DGRAM
 * AF_INET6, SOCK_DGRAM
 * AF_UNIX, SOCK_STREAM
 * AF_UNIX, SOCK_DGRAM
 * AF_VSOCK, SOCK_STREAM
 * AF_VSOCK, SOCK_SEQPACKET
 */

#include <errno.h>
#include <error.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <linux/string.h>
#include <linux/vm_sockets.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "linux/const.h"
#include "test_progs.h"
#include "sockmap_helpers.h"
#include "test_sockmap_redir.skel.h"

/* The meaning of SUPPORTED is "will redirect packet as expected".
 */
#define SUPPORTED		_BITUL(0)

/* Note on sk_skb-to-ingress ->af_vsock:
 *
 * Peer socket may receive the packet some time after the return from sendmsg().
 * In a typical usage scenario, recvmsg() will block until the redirected packet
 * appears in the destination queue, or timeout if the packet was dropped. By
 * that point, the verdict map has already been updated to reflect what has
 * happened.
 *
 * But sk_skb-to-ingress/af_vsock is an unsupported combination, so no recvmsg()
 * takes place. Which means we may race the execution of the verdict logic and
 * read map_verd before it has been updated, i.e. we might observe
 * map_verd[SK_DROP]=0 instead of map_verd[SK_DROP]=1.
 *
 * This confuses the selftest logic: if there was no packet dropped, where's the
 * packet? So here's a heuristic: on map_verd[SK_DROP]=map_verd[SK_PASS]=0
 * (which implies the verdict program has not been ran) just re-read the verdict
 * map again.
 */
#define UNSUPPORTED_RACY_VERD	_BITUL(1)

enum prog_type {
	SK_MSG_EGRESS,
	SK_MSG_INGRESS,
	SK_SKB_EGRESS,
	SK_SKB_INGRESS,
};

enum {
	SEND_INNER = 0,
	SEND_OUTER,
};

enum {
	RECV_INNER = 0,
	RECV_OUTER,
};

struct maps {
	int in;
	int out;
	int verd;
};

struct combo_spec {
	enum prog_type prog_type;
	const char *in, *out;
};

struct redir_spec {
	const char *name;
	int idx_send;
	int idx_recv;
	enum prog_type prog_type;
};

struct socket_spec {
	int family;
	int sotype;
	int send_flags;
	int in[2];
	int out[2];
};

static int socket_spec_pairs(struct socket_spec *s)
{
	return create_socket_pairs(s->family, s->sotype,
				   &s->in[0], &s->out[0],
				   &s->in[1], &s->out[1]);
}

static void socket_spec_close(struct socket_spec *s)
{
	xclose(s->in[0]);
	xclose(s->in[1]);
	xclose(s->out[0]);
	xclose(s->out[1]);
}

static void get_redir_params(struct redir_spec *redir,
			     struct test_sockmap_redir *skel, int *prog_fd,
			     enum bpf_attach_type *attach_type,
			     int *redirect_flags)
{
	enum prog_type type = redir->prog_type;
	struct bpf_program *prog;
	bool sk_msg;

	sk_msg = type == SK_MSG_INGRESS || type == SK_MSG_EGRESS;
	prog = sk_msg ? skel->progs.prog_msg_verdict : skel->progs.prog_skb_verdict;

	*prog_fd = bpf_program__fd(prog);
	*attach_type = sk_msg ? BPF_SK_MSG_VERDICT : BPF_SK_SKB_VERDICT;

	if (type == SK_MSG_INGRESS || type == SK_SKB_INGRESS)
		*redirect_flags = BPF_F_INGRESS;
	else
		*redirect_flags = 0;
}

static void try_recv(const char *prefix, int fd, int flags, bool expect_success)
{
	ssize_t n;
	char buf;

	errno = 0;
	n = recv(fd, &buf, 1, flags);
	if (n < 0 && expect_success)
		FAIL_ERRNO("%s: unexpected failure: retval=%zd", prefix, n);
	if (!n && !expect_success)
		FAIL("%s: expected failure: retval=%zd", prefix, n);
}

static void handle_unsupported(int sd_send, int sd_peer, int sd_in, int sd_out,
			       int sd_recv, int map_verd, int status)
{
	unsigned int drop, pass;
	char recv_buf;
	ssize_t n;

get_verdict:
	if (xbpf_map_lookup_elem(map_verd, &u32(SK_DROP), &drop) ||
	    xbpf_map_lookup_elem(map_verd, &u32(SK_PASS), &pass))
		return;

	if (pass == 0 && drop == 0 && (status & UNSUPPORTED_RACY_VERD)) {
		sched_yield();
		goto get_verdict;
	}

	if (pass != 0) {
		FAIL("unsupported: wanted verdict pass 0, have %u", pass);
		return;
	}

	/* If nothing was dropped, packet should have reached the peer */
	if (drop == 0) {
		errno = 0;
		n = recv_timeout(sd_peer, &recv_buf, 1, 0, IO_TIMEOUT_SEC);
		if (n != 1)
			FAIL_ERRNO("unsupported: packet missing, retval=%zd", n);
	}

	/* Ensure queues are empty */
	try_recv("bpf.recv(sd_send)", sd_send, MSG_DONTWAIT, false);
	if (sd_in != sd_send)
		try_recv("bpf.recv(sd_in)", sd_in, MSG_DONTWAIT, false);

	try_recv("bpf.recv(sd_out)", sd_out, MSG_DONTWAIT, false);
	if (sd_recv != sd_out)
		try_recv("bpf.recv(sd_recv)", sd_recv, MSG_DONTWAIT, false);
}

static void test_send_redir_recv(int sd_send, int send_flags, int sd_peer,
				 int sd_in, int sd_out, int sd_recv,
				 struct maps *maps, int status)
{
	unsigned int drop, pass;
	char *send_buf = "ab";
	char recv_buf = '\0';
	ssize_t n, len = 1;

	/* Zero out the verdict map */
	if (xbpf_map_update_elem(maps->verd, &u32(SK_DROP), &u32(0), BPF_ANY) ||
	    xbpf_map_update_elem(maps->verd, &u32(SK_PASS), &u32(0), BPF_ANY))
		return;

	if (xbpf_map_update_elem(maps->in, &u32(0), &u64(sd_in), BPF_NOEXIST))
		return;

	if (xbpf_map_update_elem(maps->out, &u32(0), &u64(sd_out), BPF_NOEXIST))
		goto del_in;

	/* Last byte is OOB data when send_flags has MSG_OOB bit set */
	if (send_flags & MSG_OOB)
		len++;
	n = send(sd_send, send_buf, len, send_flags);
	if (n >= 0 && n < len)
		FAIL("incomplete send");
	if (n < 0) {
		/* sk_msg redirect combo not supported? */
		if (status & SUPPORTED || errno != EACCES)
			FAIL_ERRNO("send");
		goto out;
	}

	if (!(status & SUPPORTED)) {
		handle_unsupported(sd_send, sd_peer, sd_in, sd_out, sd_recv,
				   maps->verd, status);
		goto out;
	}

	errno = 0;
	n = recv_timeout(sd_recv, &recv_buf, 1, 0, IO_TIMEOUT_SEC);
	if (n != 1) {
		FAIL_ERRNO("recv_timeout()");
		goto out;
	}

	/* Check verdict _after_ recv(); af_vsock may need time to catch up */
	if (xbpf_map_lookup_elem(maps->verd, &u32(SK_DROP), &drop) ||
	    xbpf_map_lookup_elem(maps->verd, &u32(SK_PASS), &pass))
		goto out;

	if (drop != 0 || pass != 1)
		FAIL("unexpected verdict drop/pass: wanted 0/1, have %u/%u",
		     drop, pass);

	if (recv_buf != send_buf[0])
		FAIL("recv(): payload check, %02x != %02x", recv_buf, send_buf[0]);

	if (send_flags & MSG_OOB) {
		/* Fail reading OOB while in sockmap */
		try_recv("bpf.recv(sd_out, MSG_OOB)", sd_out,
			 MSG_OOB | MSG_DONTWAIT, false);

		/* Remove sd_out from sockmap */
		xbpf_map_delete_elem(maps->out, &u32(0));

		/* Check that OOB was dropped on redirect */
		try_recv("recv(sd_out, MSG_OOB)", sd_out,
			 MSG_OOB | MSG_DONTWAIT, false);

		goto del_in;
	}
out:
	xbpf_map_delete_elem(maps->out, &u32(0));
del_in:
	xbpf_map_delete_elem(maps->in, &u32(0));
}

static int is_redir_supported(enum prog_type type, const char *in,
			      const char *out)
{
	/* Matching based on strings returned by socket_kind_to_str():
	 * tcp4, udp4, tcp6, udp6, u_str, u_dgr, v_str, v_seq
	 * Plus a wildcard: any
	 * Not in use: u_seq, v_dgr
	 */
	struct combo_spec *c, combos[] = {
		/* Send to local: TCP -> any, but vsock */
		{ SK_MSG_INGRESS,	"tcp",	"tcp"	},
		{ SK_MSG_INGRESS,	"tcp",	"udp"	},
		{ SK_MSG_INGRESS,	"tcp",	"u_str"	},
		{ SK_MSG_INGRESS,	"tcp",	"u_dgr"	},

		/* Send to egress: TCP -> TCP */
		{ SK_MSG_EGRESS,	"tcp",	"tcp"	},

		/* Ingress to egress: any -> any */
		{ SK_SKB_EGRESS,	"any",	"any"	},

		/* Ingress to local: any -> any, but vsock */
		{ SK_SKB_INGRESS,	"any",	"tcp"	},
		{ SK_SKB_INGRESS,	"any",	"udp"	},
		{ SK_SKB_INGRESS,	"any",	"u_str"	},
		{ SK_SKB_INGRESS,	"any",	"u_dgr"	},
	};

	for (c = combos; c < combos + ARRAY_SIZE(combos); c++) {
		if (c->prog_type == type &&
		    (!strcmp(c->in, "any") || strstarts(in, c->in)) &&
		    (!strcmp(c->out, "any") || strstarts(out, c->out)))
			return SUPPORTED;
	}

	return 0;
}

static int get_support_status(enum prog_type type, const char *in,
			      const char *out)
{
	int status = is_redir_supported(type, in, out);

	if (type == SK_SKB_INGRESS && strstarts(out, "v_"))
		status |= UNSUPPORTED_RACY_VERD;

	return status;
}

static void test_socket(enum bpf_map_type type, struct redir_spec *redir,
			struct maps *maps, struct socket_spec *s_in,
			struct socket_spec *s_out)
{
	int fd_in, fd_out, fd_send, fd_peer, fd_recv, flags, status;
	const char *in_str, *out_str;
	char s[MAX_TEST_NAME];

	fd_in = s_in->in[0];
	fd_out = s_out->out[0];
	fd_send = s_in->in[redir->idx_send];
	fd_peer = s_in->in[redir->idx_send ^ 1];
	fd_recv = s_out->out[redir->idx_recv];
	flags = s_in->send_flags;

	in_str = socket_kind_to_str(fd_in);
	out_str = socket_kind_to_str(fd_out);
	status = get_support_status(redir->prog_type, in_str, out_str);

	snprintf(s, sizeof(s),
		 "%-4s %-17s %-5s %s %-5s%6s",
		 /* hash sk_skb-to-ingress u_str → v_str (OOB) */
		 type == BPF_MAP_TYPE_SOCKMAP ? "map" : "hash",
		 redir->name,
		 in_str,
		 status & SUPPORTED ? "→" : " ",
		 out_str,
		 (flags & MSG_OOB) ? "(OOB)" : "");

	if (!test__start_subtest(s))
		return;

	test_send_redir_recv(fd_send, flags, fd_peer, fd_in, fd_out, fd_recv,
			     maps, status);
}

static void test_redir(enum bpf_map_type type, struct redir_spec *redir,
		       struct maps *maps)
{
	struct socket_spec *s, sockets[] = {
		{ AF_INET, SOCK_STREAM },
		// { AF_INET, SOCK_STREAM, MSG_OOB }, /* Known to be broken */
		{ AF_INET6, SOCK_STREAM },
		{ AF_INET, SOCK_DGRAM },
		{ AF_INET6, SOCK_DGRAM },
		{ AF_UNIX, SOCK_STREAM },
		{ AF_UNIX, SOCK_STREAM, MSG_OOB },
		{ AF_UNIX, SOCK_DGRAM },
		// { AF_UNIX, SOCK_SEQPACKET},	/* Unsupported BPF_MAP_UPDATE_ELEM */
		{ AF_VSOCK, SOCK_STREAM },
		// { AF_VSOCK, SOCK_DGRAM },	/* Unsupported socket() */
		{ AF_VSOCK, SOCK_SEQPACKET },
	};

	for (s = sockets; s < sockets + ARRAY_SIZE(sockets); s++)
		if (socket_spec_pairs(s))
			goto out;

	/* Intra-proto */
	for (s = sockets; s < sockets + ARRAY_SIZE(sockets); s++)
		test_socket(type, redir, maps, s, s);

	/* Cross-proto */
	for (int i = 0; i < ARRAY_SIZE(sockets); i++) {
		for (int j = 0; j < ARRAY_SIZE(sockets); j++) {
			struct socket_spec *out = &sockets[j];
			struct socket_spec *in = &sockets[i];

			/* Skip intra-proto and between variants */
			if (out->send_flags ||
			    (in->family == out->family &&
			     in->sotype == out->sotype))
				continue;

			test_socket(type, redir, maps, in, out);
		}
	}
out:
	while (--s >= sockets)
		socket_spec_close(s);
}

static void test_map(enum bpf_map_type type)
{
	struct redir_spec *r, redirs[] = {
		{ "sk_msg-to-ingress", SEND_INNER, RECV_INNER, SK_MSG_INGRESS },
		{ "sk_msg-to-egress", SEND_INNER, RECV_OUTER, SK_MSG_EGRESS },
		{ "sk_skb-to-egress", SEND_OUTER, RECV_OUTER, SK_SKB_EGRESS },
		{ "sk_skb-to-ingress", SEND_OUTER, RECV_INNER, SK_SKB_INGRESS },
	};

	for (r = redirs; r < redirs + ARRAY_SIZE(redirs); r++) {
		enum bpf_attach_type attach_type;
		struct test_sockmap_redir *skel;
		struct maps maps;
		int prog_fd;

		skel = test_sockmap_redir__open_and_load();
		if (!skel) {
			FAIL("open_and_load");
			return;
		}

		switch (type) {
		case BPF_MAP_TYPE_SOCKMAP:
			maps.in = bpf_map__fd(skel->maps.nop_map);
			maps.out = bpf_map__fd(skel->maps.sock_map);
			break;
		case BPF_MAP_TYPE_SOCKHASH:
			maps.in = bpf_map__fd(skel->maps.nop_hash);
			maps.out = bpf_map__fd(skel->maps.sock_hash);
			break;
		default:
			FAIL("Unsupported bpf_map_type");
			return;
		}

		skel->bss->redirect_type = type;
		maps.verd = bpf_map__fd(skel->maps.verdict_map);
		get_redir_params(r, skel, &prog_fd, &attach_type,
				 &skel->bss->redirect_flags);

		if (xbpf_prog_attach(prog_fd, maps.in, attach_type, 0))
			return;

		test_redir(type, r, &maps);

		if (xbpf_prog_detach2(prog_fd, maps.in, attach_type))
			return;

		test_sockmap_redir__destroy(skel);
	}
}

void serial_test_sockmap_redir(void)
{
	test_map(BPF_MAP_TYPE_SOCKMAP);
	test_map(BPF_MAP_TYPE_SOCKHASH);
}
