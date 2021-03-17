#include <uapi/linux/bpf.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <uapi/linux/in.h>
#include <uapi/linux/in6.h>
#include <bpf/bpf_helpers.h>

SEC("cgroup/sock1")
int bpf_prog1(struct bpf_sock *sk)
{
	char fmt[] = "socket: family %d type %d protocol %d\n";
	char fmt2[] = "socket: uid %u gid %u\n";
	__u64 gid_uid = bpf_get_current_uid_gid();
	__u32 uid = gid_uid & 0xffffffff;
	__u32 gid = gid_uid >> 32;

	bpf_trace_printk(fmt, sizeof(fmt), sk->family, sk->type, sk->protocol);
	bpf_trace_printk(fmt2, sizeof(fmt2), uid, gid);

	/* block PF_INET6, SOCK_RAW, IPPROTO_ICMPV6 sockets
	 * ie., make ping6 fail
	 */
	if (sk->family == PF_INET6 &&
	    sk->type == SOCK_RAW   &&
	    sk->protocol == IPPROTO_ICMPV6)
		return 0;

	return 1;
}

SEC("cgroup/sock2")
int bpf_prog2(struct bpf_sock *sk)
{
	char fmt[] = "socket: family %d type %d protocol %d\n";

	bpf_trace_printk(fmt, sizeof(fmt), sk->family, sk->type, sk->protocol);

	/* block PF_INET, SOCK_RAW, IPPROTO_ICMP sockets
	 * ie., make ping fail
	 */
	if (sk->family == PF_INET &&
	    sk->type == SOCK_RAW  &&
	    sk->protocol == IPPROTO_ICMP)
		return 0;

	return 1;
}

char _license[] SEC("license") = "GPL";
