#ifndef _SDP_DBG_H_
#define _SDP_DBG_H_

#define SDPSTATS_ON

//#define GETNSTIMEODAY_SUPPORTED

#define _sdp_printk(func, line, level, sk, format, arg...)	\
do {								\
	printk(level "%s:%d %p sdp_sock(%d:%d %d:%d): " format "\n",	\
	       func, line, sk ? sdp_sk(sk) : NULL,		\
	       curproc->p_pid, PCPU_GET(cpuid),			\
	       (sk) && sdp_sk(sk) ? ntohs(sdp_sk(sk)->lport) : -1,	\
	       (sk) && sdp_sk(sk) ? ntohs(sdp_sk(sk)->fport) : -1, ## arg);	\
} while (0)
#define sdp_printk(level, sk, format, arg...)                \
	_sdp_printk(__func__, __LINE__, level, sk, format, ## arg)
#define sdp_warn(sk, format, arg...)                         \
	sdp_printk(KERN_WARNING, sk, format , ## arg)

#define SDP_MODPARAM_SINT(var, def_val, msg) \
	static int var = def_val; \
	module_param_named(var, var, int, 0644); \
	MODULE_PARM_DESC(var, msg " [" #def_val "]"); \

#define SDP_MODPARAM_INT(var, def_val, msg) \
	int var = def_val; \
	module_param_named(var, var, int, 0644); \
	MODULE_PARM_DESC(var, msg " [" #def_val "]"); \

#ifdef SDP_PROFILING
struct mbuf;
struct sdpprf_log {
	int 		idx;
	int 		pid;
	int 		cpu;
	int 		sk_num;
	int 		sk_dport;
	struct mbuf 	*mb;
	char		msg[256];

	unsigned long long time;

	const char 	*func;
	int 		line;
};

#define SDPPRF_LOG_SIZE 0x20000 /* must be a power of 2 */

extern struct sdpprf_log sdpprf_log[SDPPRF_LOG_SIZE];
extern int sdpprf_log_count;

#ifdef GETNSTIMEODAY_SUPPORTED
static inline unsigned long long current_nsec(void)
{
	struct timespec tv;
	getnstimeofday(&tv);
	return tv.tv_sec * NSEC_PER_SEC + tv.tv_nsec;
}
#else
#define current_nsec() jiffies_to_usecs(jiffies)
#endif

#define sdp_prf1(sk, s, format, arg...) ({ \
	struct sdpprf_log *l = \
		&sdpprf_log[sdpprf_log_count++ & (SDPPRF_LOG_SIZE - 1)]; \
	preempt_disable(); \
	l->idx = sdpprf_log_count - 1; \
	l->pid = current->pid; \
	l->sk_num = (sk) ? inet_sk(sk)->num : -1;                 \
	l->sk_dport = (sk) ? ntohs(inet_sk(sk)->dport) : -1; \
	l->cpu = smp_processor_id(); \
	l->mb = s; \
	snprintf(l->msg, sizeof(l->msg) - 1, format, ## arg); \
	l->time = current_nsec(); \
	l->func = __func__; \
	l->line = __LINE__; \
	preempt_enable(); \
	1; \
})
//#define sdp_prf(sk, s, format, arg...)
#define sdp_prf(sk, s, format, arg...) sdp_prf1(sk, s, format, ## arg)

#else
#define sdp_prf1(sk, s, format, arg...)
#define sdp_prf(sk, s, format, arg...)
#endif

#ifdef CONFIG_INFINIBAND_SDP_DEBUG
extern int sdp_debug_level;

#define sdp_dbg(sk, format, arg...)                          \
	do {                                                 \
		if (sdp_debug_level > 0)                     \
		sdp_printk(KERN_WARNING, sk, format , ## arg); \
	} while (0)

#else /* CONFIG_INFINIBAND_SDP_DEBUG */
#define sdp_dbg(priv, format, arg...)                        \
	do { (void) (priv); } while (0)
#define sock_ref(sk, msg, sock_op) sock_op(sk)
#endif /* CONFIG_INFINIBAND_SDP_DEBUG */

#ifdef CONFIG_INFINIBAND_SDP_DEBUG_DATA

extern int sdp_data_debug_level;
#define sdp_dbg_data(sk, format, arg...)                     		\
	do {                                                 		\
		if (sdp_data_debug_level & 0x2)                		\
			sdp_printk(KERN_WARNING, sk, format , ## arg); 	\
	} while (0)
#define SDP_DUMP_PACKET(sk, str, mb, h)                     		\
	do {                                                 		\
		if (sdp_data_debug_level & 0x1)                		\
			dump_packet(sk, str, mb, h); 			\
	} while (0)
#else
#define sdp_dbg_data(priv, format, arg...)
#define SDP_DUMP_PACKET(sk, str, mb, h)
#endif

#define SOCK_REF_RESET "RESET"
#define SOCK_REF_ALIVE "ALIVE" /* sock_alloc -> destruct_sock */
#define SOCK_REF_CLONE "CLONE"
#define SOCK_REF_CMA "CMA" /* sdp_cma_handler() is expected to be invoked */
#define SOCK_REF_SEQ "SEQ" /* during proc read */
#define SOCK_REF_DREQ_TO "DREQ_TO" /* dreq timeout is pending */
#define SOCK_REF_ZCOPY "ZCOPY" /* zcopy send in process */
#define SOCK_REF_RDMA_RD "RDMA_RD" /* RDMA read in process */

#define sock_hold(sk, msg)  sock_ref(sk, msg, sock_hold)
#define sock_put(sk, msg)  sock_ref(sk, msg, sock_put)
#define __sock_put(sk, msg)  sock_ref(sk, msg, __sock_put)

#define ENUM2STR(e) [e] = #e

static inline char *sdp_state_str(int state)
{
	static char *state2str[] = {
		ENUM2STR(TCPS_ESTABLISHED),
		ENUM2STR(TCPS_SYN_SENT),
		ENUM2STR(TCPS_SYN_RECEIVED),
		ENUM2STR(TCPS_FIN_WAIT_1),
		ENUM2STR(TCPS_FIN_WAIT_2),
		ENUM2STR(TCPS_TIME_WAIT),
		ENUM2STR(TCPS_CLOSED),
		ENUM2STR(TCPS_CLOSE_WAIT),
		ENUM2STR(TCPS_LAST_ACK),
		ENUM2STR(TCPS_LISTEN),
		ENUM2STR(TCPS_CLOSING),
	};

	if (state < 0 || state >= ARRAY_SIZE(state2str))
		return "unknown";

	return state2str[state];
}

struct sdp_bsdh;
#ifdef CONFIG_INFINIBAND_SDP_DEBUG_DATA
void _dump_packet(const char *func, int line, struct socket *sk, char *str,
		struct mbuf *mb, const struct sdp_bsdh *h);
#define dump_packet(sk, str, mb, h) \
	_dump_packet(__func__, __LINE__, sk, str, mb, h)
#endif

#endif
