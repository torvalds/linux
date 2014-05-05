/*
 * Linux Socket Filter Data Structures
 */
#ifndef __LINUX_FILTER_H__
#define __LINUX_FILTER_H__

#include <linux/atomic.h>
#include <linux/compat.h>
#include <linux/workqueue.h>
#include <uapi/linux/filter.h>

/* Internally used and optimized filter representation with extended
 * instruction set based on top of classic BPF.
 */

/* instruction classes */
#define BPF_ALU64	0x07	/* alu mode in double word width */

/* ld/ldx fields */
#define BPF_DW		0x18	/* double word */
#define BPF_XADD	0xc0	/* exclusive add */

/* alu/jmp fields */
#define BPF_MOV		0xb0	/* mov reg to reg */
#define BPF_ARSH	0xc0	/* sign extending arithmetic shift right */

/* change endianness of a register */
#define BPF_END		0xd0	/* flags for endianness conversion: */
#define BPF_TO_LE	0x00	/* convert to little-endian */
#define BPF_TO_BE	0x08	/* convert to big-endian */
#define BPF_FROM_LE	BPF_TO_LE
#define BPF_FROM_BE	BPF_TO_BE

#define BPF_JNE		0x50	/* jump != */
#define BPF_JSGT	0x60	/* SGT is signed '>', GT in x86 */
#define BPF_JSGE	0x70	/* SGE is signed '>=', GE in x86 */
#define BPF_CALL	0x80	/* function call */
#define BPF_EXIT	0x90	/* function return */

/* BPF has 10 general purpose 64-bit registers and stack frame. */
#define MAX_BPF_REG	11

/* BPF program can access up to 512 bytes of stack space. */
#define MAX_BPF_STACK	512

/* Arg1, context and stack frame pointer register positions. */
#define ARG1_REG	1
#define CTX_REG		6
#define FP_REG		10

struct sock_filter_int {
	__u8	code;		/* opcode */
	__u8	a_reg:4;	/* dest register */
	__u8	x_reg:4;	/* source register */
	__s16	off;		/* signed offset */
	__s32	imm;		/* signed immediate constant */
};

#ifdef CONFIG_COMPAT
/* A struct sock_filter is architecture independent. */
struct compat_sock_fprog {
	u16		len;
	compat_uptr_t	filter;	/* struct sock_filter * */
};
#endif

struct sock_fprog_kern {
	u16			len;
	struct sock_filter	*filter;
};

struct sk_buff;
struct sock;
struct seccomp_data;

struct sk_filter {
	atomic_t		refcnt;
	u32			jited:1,	/* Is our filter JIT'ed? */
				len:31;		/* Number of filter blocks */
	struct sock_fprog_kern	*orig_prog;	/* Original BPF program */
	struct rcu_head		rcu;
	unsigned int		(*bpf_func)(const struct sk_buff *skb,
					    const struct sock_filter_int *filter);
	union {
		struct sock_filter	insns[0];
		struct sock_filter_int	insnsi[0];
		struct work_struct	work;
	};
};

static inline unsigned int sk_filter_size(unsigned int proglen)
{
	return max(sizeof(struct sk_filter),
		   offsetof(struct sk_filter, insns[proglen]));
}

#define sk_filter_proglen(fprog)			\
		(fprog->len * sizeof(fprog->filter[0]))

#define SK_RUN_FILTER(filter, ctx)			\
		(*filter->bpf_func)(ctx, filter->insnsi)

int sk_filter(struct sock *sk, struct sk_buff *skb);

u32 sk_run_filter_int_seccomp(const struct seccomp_data *ctx,
			      const struct sock_filter_int *insni);
u32 sk_run_filter_int_skb(const struct sk_buff *ctx,
			  const struct sock_filter_int *insni);

int sk_convert_filter(struct sock_filter *prog, int len,
		      struct sock_filter_int *new_prog, int *new_len);

int sk_unattached_filter_create(struct sk_filter **pfp,
				struct sock_fprog *fprog);
void sk_unattached_filter_destroy(struct sk_filter *fp);

int sk_attach_filter(struct sock_fprog *fprog, struct sock *sk);
int sk_detach_filter(struct sock *sk);

int sk_chk_filter(struct sock_filter *filter, unsigned int flen);
int sk_get_filter(struct sock *sk, struct sock_filter __user *filter,
		  unsigned int len);
void sk_decode_filter(struct sock_filter *filt, struct sock_filter *to);

void sk_filter_charge(struct sock *sk, struct sk_filter *fp);
void sk_filter_uncharge(struct sock *sk, struct sk_filter *fp);

#ifdef CONFIG_BPF_JIT
#include <stdarg.h>
#include <linux/linkage.h>
#include <linux/printk.h>

void bpf_jit_compile(struct sk_filter *fp);
void bpf_jit_free(struct sk_filter *fp);

static inline void bpf_jit_dump(unsigned int flen, unsigned int proglen,
				u32 pass, void *image)
{
	pr_err("flen=%u proglen=%u pass=%u image=%pK\n",
	       flen, proglen, pass, image);
	if (image)
		print_hex_dump(KERN_ERR, "JIT code: ", DUMP_PREFIX_OFFSET,
			       16, 1, image, proglen, false);
}
#else
#include <linux/slab.h>
static inline void bpf_jit_compile(struct sk_filter *fp)
{
}
static inline void bpf_jit_free(struct sk_filter *fp)
{
	kfree(fp);
}
#endif

static inline int bpf_tell_extensions(void)
{
	return SKF_AD_MAX;
}

enum {
	BPF_S_RET_K = 1,
	BPF_S_RET_A,
	BPF_S_ALU_ADD_K,
	BPF_S_ALU_ADD_X,
	BPF_S_ALU_SUB_K,
	BPF_S_ALU_SUB_X,
	BPF_S_ALU_MUL_K,
	BPF_S_ALU_MUL_X,
	BPF_S_ALU_DIV_X,
	BPF_S_ALU_MOD_K,
	BPF_S_ALU_MOD_X,
	BPF_S_ALU_AND_K,
	BPF_S_ALU_AND_X,
	BPF_S_ALU_OR_K,
	BPF_S_ALU_OR_X,
	BPF_S_ALU_XOR_K,
	BPF_S_ALU_XOR_X,
	BPF_S_ALU_LSH_K,
	BPF_S_ALU_LSH_X,
	BPF_S_ALU_RSH_K,
	BPF_S_ALU_RSH_X,
	BPF_S_ALU_NEG,
	BPF_S_LD_W_ABS,
	BPF_S_LD_H_ABS,
	BPF_S_LD_B_ABS,
	BPF_S_LD_W_LEN,
	BPF_S_LD_W_IND,
	BPF_S_LD_H_IND,
	BPF_S_LD_B_IND,
	BPF_S_LD_IMM,
	BPF_S_LDX_W_LEN,
	BPF_S_LDX_B_MSH,
	BPF_S_LDX_IMM,
	BPF_S_MISC_TAX,
	BPF_S_MISC_TXA,
	BPF_S_ALU_DIV_K,
	BPF_S_LD_MEM,
	BPF_S_LDX_MEM,
	BPF_S_ST,
	BPF_S_STX,
	BPF_S_JMP_JA,
	BPF_S_JMP_JEQ_K,
	BPF_S_JMP_JEQ_X,
	BPF_S_JMP_JGE_K,
	BPF_S_JMP_JGE_X,
	BPF_S_JMP_JGT_K,
	BPF_S_JMP_JGT_X,
	BPF_S_JMP_JSET_K,
	BPF_S_JMP_JSET_X,
	/* Ancillary data */
	BPF_S_ANC_PROTOCOL,
	BPF_S_ANC_PKTTYPE,
	BPF_S_ANC_IFINDEX,
	BPF_S_ANC_NLATTR,
	BPF_S_ANC_NLATTR_NEST,
	BPF_S_ANC_MARK,
	BPF_S_ANC_QUEUE,
	BPF_S_ANC_HATYPE,
	BPF_S_ANC_RXHASH,
	BPF_S_ANC_CPU,
	BPF_S_ANC_ALU_XOR_X,
	BPF_S_ANC_VLAN_TAG,
	BPF_S_ANC_VLAN_TAG_PRESENT,
	BPF_S_ANC_PAY_OFFSET,
};

#endif /* __LINUX_FILTER_H__ */
