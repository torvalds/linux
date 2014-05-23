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

/* Register numbers */
enum {
	BPF_REG_0 = 0,
	BPF_REG_1,
	BPF_REG_2,
	BPF_REG_3,
	BPF_REG_4,
	BPF_REG_5,
	BPF_REG_6,
	BPF_REG_7,
	BPF_REG_8,
	BPF_REG_9,
	BPF_REG_10,
	__MAX_BPF_REG,
};

/* BPF has 10 general purpose 64-bit registers and stack frame. */
#define MAX_BPF_REG	__MAX_BPF_REG

/* ArgX, context and stack frame pointer register positions. Note,
 * Arg1, Arg2, Arg3, etc are used as argument mappings of function
 * calls in BPF_CALL instruction.
 */
#define BPF_REG_ARG1	BPF_REG_1
#define BPF_REG_ARG2	BPF_REG_2
#define BPF_REG_ARG3	BPF_REG_3
#define BPF_REG_ARG4	BPF_REG_4
#define BPF_REG_ARG5	BPF_REG_5
#define BPF_REG_CTX	BPF_REG_6
#define BPF_REG_FP	BPF_REG_10

/* Additional register mappings for converted user programs. */
#define BPF_REG_A	BPF_REG_0
#define BPF_REG_X	BPF_REG_7
#define BPF_REG_TMP	BPF_REG_8

/* BPF program can access up to 512 bytes of stack space. */
#define MAX_BPF_STACK	512

/* bpf_add|sub|...: a += x, bpf_mov: a = x */
#define BPF_ALU64_REG(op, a, x) \
	((struct sock_filter_int) {BPF_ALU64|BPF_OP(op)|BPF_X, a, x, 0, 0})
#define BPF_ALU32_REG(op, a, x) \
	((struct sock_filter_int) {BPF_ALU|BPF_OP(op)|BPF_X, a, x, 0, 0})

/* bpf_add|sub|...: a += imm, bpf_mov: a = imm */
#define BPF_ALU64_IMM(op, a, imm) \
	((struct sock_filter_int) {BPF_ALU64|BPF_OP(op)|BPF_K, a, 0, 0, imm})
#define BPF_ALU32_IMM(op, a, imm) \
	((struct sock_filter_int) {BPF_ALU|BPF_OP(op)|BPF_K, a, 0, 0, imm})

/* R0 = *(uint *) (skb->data + off) */
#define BPF_LD_ABS(size, off) \
	((struct sock_filter_int) {BPF_LD|BPF_SIZE(size)|BPF_ABS, 0, 0, 0, off})

/* R0 = *(uint *) (skb->data + x + off) */
#define BPF_LD_IND(size, x, off) \
	((struct sock_filter_int) {BPF_LD|BPF_SIZE(size)|BPF_IND, 0, x, 0, off})

/* a = *(uint *) (x + off) */
#define BPF_LDX_MEM(sz, a, x, off) \
	((struct sock_filter_int) {BPF_LDX|BPF_SIZE(sz)|BPF_MEM, a, x, off, 0})

/* if (a 'op' x) goto pc+off */
#define BPF_JMP_REG(op, a, x, off) \
	((struct sock_filter_int) {BPF_JMP|BPF_OP(op)|BPF_X, a, x, off, 0})

/* if (a 'op' imm) goto pc+off */
#define BPF_JMP_IMM(op, a, imm, off) \
	((struct sock_filter_int) {BPF_JMP|BPF_OP(op)|BPF_K, a, 0, off, imm})

#define BPF_EXIT_INSN() \
	((struct sock_filter_int) {BPF_JMP|BPF_EXIT, 0, 0, 0, 0})

static inline int size_to_bpf(int size)
{
	switch (size) {
	case 1:
		return BPF_B;
	case 2:
		return BPF_H;
	case 4:
		return BPF_W;
	case 8:
		return BPF_DW;
	default:
		return -EINVAL;
	}
}

/* Macro to invoke filter function. */
#define SK_RUN_FILTER(filter, ctx)  (*filter->bpf_func)(ctx, filter->insnsi)

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

int sk_filter(struct sock *sk, struct sk_buff *skb);

void sk_filter_select_runtime(struct sk_filter *fp);
void sk_filter_free(struct sk_filter *fp);

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

u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
void bpf_int_jit_compile(struct sk_filter *fp);

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
	BPF_S_ANC_RANDOM,
};

#endif /* __LINUX_FILTER_H__ */
