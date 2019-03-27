/*
 * $FreeBSD$
 */

struct krping_stats {
	unsigned long long send_bytes;
	unsigned long long send_msgs;
	unsigned long long recv_bytes;
	unsigned long long recv_msgs;
	unsigned long long write_bytes;
	unsigned long long write_msgs;
	unsigned long long read_bytes;
	unsigned long long read_msgs;
	char name[16];
};

int krping_doit(char *);
void krping_walk_cb_list(void (*)(struct krping_stats *, void *), void *);
int krping_sigpending(void);
