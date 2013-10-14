#if !defined(_TRACE_KVM_MAIN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_MAIN_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

#define ERSN(x) { KVM_EXIT_##x, "KVM_EXIT_" #x }

#define kvm_trace_exit_reason						\
	ERSN(UNKNOWN), ERSN(EXCEPTION), ERSN(IO), ERSN(HYPERCALL),	\
	ERSN(DEBUG), ERSN(HLT), ERSN(MMIO), ERSN(IRQ_WINDOW_OPEN),	\
	ERSN(SHUTDOWN), ERSN(FAIL_ENTRY), ERSN(INTR), ERSN(SET_TPR),	\
	ERSN(TPR_ACCESS), ERSN(S390_SIEIC), ERSN(S390_RESET), ERSN(DCR),\
	ERSN(NMI), ERSN(INTERNAL_ERROR), ERSN(OSI), ERSN(PAPR_HCALL),	\
	ERSN(S390_UCONTROL), ERSN(WATCHDOG), ERSN(S390_TSCH)

TRACE_EVENT(kvm_userspace_exit,
	    TP_PROTO(__u32 reason, int errno),
	    TP_ARGS(reason, errno),

	TP_STRUCT__entry(
		__field(	__u32,		reason		)
		__field(	int,		errno		)
	),

	TP_fast_assign(
		__entry->reason		= reason;
		__entry->errno		= errno;
	),

	TP_printk("reason %s (%d)",
		  __entry->errno < 0 ?
		  (__entry->errno == -EINTR ? "restart" : "error") :
		  __print_symbolic(__entry->reason, kvm_trace_exit_reason),
		  __entry->errno < 0 ? -__entry->errno : __entry->reason)
);

#if defined(CONFIG_HAVE_KVM_IRQCHIP)
TRACE_EVENT(kvm_set_irq,
	TP_PROTO(unsigned int gsi, int level, int irq_source_id),
	TP_ARGS(gsi, level, irq_source_id),

	TP_STRUCT__entry(
		__field(	unsigned int,	gsi		)
		__field(	int,		level		)
		__field(	int,		irq_source_id	)
	),

	TP_fast_assign(
		__entry->gsi		= gsi;
		__entry->level		= level;
		__entry->irq_source_id	= irq_source_id;
	),

	TP_printk("gsi %u level %d source %d",
		  __entry->gsi, __entry->level, __entry->irq_source_id)
);
#endif

#if defined(__KVM_HAVE_IOAPIC)
#define kvm_deliver_mode		\
	{0x0, "Fixed"},			\
	{0x1, "LowPrio"},		\
	{0x2, "SMI"},			\
	{0x3, "Res3"},			\
	{0x4, "NMI"},			\
	{0x5, "INIT"},			\
	{0x6, "SIPI"},			\
	{0x7, "ExtINT"}

TRACE_EVENT(kvm_ioapic_set_irq,
	    TP_PROTO(__u64 e, int pin, bool coalesced),
	    TP_ARGS(e, pin, coalesced),

	TP_STRUCT__entry(
		__field(	__u64,		e		)
		__field(	int,		pin		)
		__field(	bool,		coalesced	)
	),

	TP_fast_assign(
		__entry->e		= e;
		__entry->pin		= pin;
		__entry->coalesced	= coalesced;
	),

	TP_printk("pin %u dst %x vec=%u (%s|%s|%s%s)%s",
		  __entry->pin, (u8)(__entry->e >> 56), (u8)__entry->e,
		  __print_symbolic((__entry->e >> 8 & 0x7), kvm_deliver_mode),
		  (__entry->e & (1<<11)) ? "logical" : "physical",
		  (__entry->e & (1<<15)) ? "level" : "edge",
		  (__entry->e & (1<<16)) ? "|masked" : "",
		  __entry->coalesced ? " (coalesced)" : "")
);

TRACE_EVENT(kvm_msi_set_irq,
	    TP_PROTO(__u64 address, __u64 data),
	    TP_ARGS(address, data),

	TP_STRUCT__entry(
		__field(	__u64,		address		)
		__field(	__u64,		data		)
	),

	TP_fast_assign(
		__entry->address	= address;
		__entry->data		= data;
	),

	TP_printk("dst %u vec %x (%s|%s|%s%s)",
		  (u8)(__entry->address >> 12), (u8)__entry->data,
		  __print_symbolic((__entry->data >> 8 & 0x7), kvm_deliver_mode),
		  (__entry->address & (1<<2)) ? "logical" : "physical",
		  (__entry->data & (1<<15)) ? "level" : "edge",
		  (__entry->address & (1<<3)) ? "|rh" : "")
);

#define kvm_irqchips						\
	{KVM_IRQCHIP_PIC_MASTER,	"PIC master"},		\
	{KVM_IRQCHIP_PIC_SLAVE,		"PIC slave"},		\
	{KVM_IRQCHIP_IOAPIC,		"IOAPIC"}

#endif /* defined(__KVM_HAVE_IOAPIC) */

#if defined(CONFIG_HAVE_KVM_IRQCHIP)

TRACE_EVENT(kvm_ack_irq,
	TP_PROTO(unsigned int irqchip, unsigned int pin),
	TP_ARGS(irqchip, pin),

	TP_STRUCT__entry(
		__field(	unsigned int,	irqchip		)
		__field(	unsigned int,	pin		)
	),

	TP_fast_assign(
		__entry->irqchip	= irqchip;
		__entry->pin		= pin;
	),

#ifdef kvm_irqchips
	TP_printk("irqchip %s pin %u",
		  __print_symbolic(__entry->irqchip, kvm_irqchips),
		 __entry->pin)
#else
	TP_printk("irqchip %d pin %u", __entry->irqchip, __entry->pin)
#endif
);

#endif /* defined(CONFIG_HAVE_KVM_IRQCHIP) */



#define KVM_TRACE_MMIO_READ_UNSATISFIED 0
#define KVM_TRACE_MMIO_READ 1
#define KVM_TRACE_MMIO_WRITE 2

#define kvm_trace_symbol_mmio \
	{ KVM_TRACE_MMIO_READ_UNSATISFIED, "unsatisfied-read" }, \
	{ KVM_TRACE_MMIO_READ, "read" }, \
	{ KVM_TRACE_MMIO_WRITE, "write" }

TRACE_EVENT(kvm_mmio,
	TP_PROTO(int type, int len, u64 gpa, u64 val),
	TP_ARGS(type, len, gpa, val),

	TP_STRUCT__entry(
		__field(	u32,	type		)
		__field(	u32,	len		)
		__field(	u64,	gpa		)
		__field(	u64,	val		)
	),

	TP_fast_assign(
		__entry->type		= type;
		__entry->len		= len;
		__entry->gpa		= gpa;
		__entry->val		= val;
	),

	TP_printk("mmio %s len %u gpa 0x%llx val 0x%llx",
		  __print_symbolic(__entry->type, kvm_trace_symbol_mmio),
		  __entry->len, __entry->gpa, __entry->val)
);

#define kvm_fpu_load_symbol	\
	{0, "unload"},		\
	{1, "load"}

TRACE_EVENT(kvm_fpu,
	TP_PROTO(int load),
	TP_ARGS(load),

	TP_STRUCT__entry(
		__field(	u32,	        load		)
	),

	TP_fast_assign(
		__entry->load		= load;
	),

	TP_printk("%s", __print_symbolic(__entry->load, kvm_fpu_load_symbol))
);

TRACE_EVENT(kvm_age_page,
	TP_PROTO(ulong hva, struct kvm_memory_slot *slot, int ref),
	TP_ARGS(hva, slot, ref),

	TP_STRUCT__entry(
		__field(	u64,	hva		)
		__field(	u64,	gfn		)
		__field(	u8,	referenced	)
	),

	TP_fast_assign(
		__entry->hva		= hva;
		__entry->gfn		=
		  slot->base_gfn + ((hva - slot->userspace_addr) >> PAGE_SHIFT);
		__entry->referenced	= ref;
	),

	TP_printk("hva %llx gfn %llx %s",
		  __entry->hva, __entry->gfn,
		  __entry->referenced ? "YOUNG" : "OLD")
);

#ifdef CONFIG_KVM_ASYNC_PF
DECLARE_EVENT_CLASS(kvm_async_get_page_class,

	TP_PROTO(u64 gva, u64 gfn),

	TP_ARGS(gva, gfn),

	TP_STRUCT__entry(
		__field(__u64, gva)
		__field(u64, gfn)
	),

	TP_fast_assign(
		__entry->gva = gva;
		__entry->gfn = gfn;
	),

	TP_printk("gva = %#llx, gfn = %#llx", __entry->gva, __entry->gfn)
);

DEFINE_EVENT(kvm_async_get_page_class, kvm_try_async_get_page,

	TP_PROTO(u64 gva, u64 gfn),

	TP_ARGS(gva, gfn)
);

DEFINE_EVENT(kvm_async_get_page_class, kvm_async_pf_doublefault,

	TP_PROTO(u64 gva, u64 gfn),

	TP_ARGS(gva, gfn)
);

DECLARE_EVENT_CLASS(kvm_async_pf_nopresent_ready,

	TP_PROTO(u64 token, u64 gva),

	TP_ARGS(token, gva),

	TP_STRUCT__entry(
		__field(__u64, token)
		__field(__u64, gva)
	),

	TP_fast_assign(
		__entry->token = token;
		__entry->gva = gva;
	),

	TP_printk("token %#llx gva %#llx", __entry->token, __entry->gva)

);

DEFINE_EVENT(kvm_async_pf_nopresent_ready, kvm_async_pf_not_present,

	TP_PROTO(u64 token, u64 gva),

	TP_ARGS(token, gva)
);

DEFINE_EVENT(kvm_async_pf_nopresent_ready, kvm_async_pf_ready,

	TP_PROTO(u64 token, u64 gva),

	TP_ARGS(token, gva)
);

TRACE_EVENT(
	kvm_async_pf_completed,
	TP_PROTO(unsigned long address, u64 gva),
	TP_ARGS(address, gva),

	TP_STRUCT__entry(
		__field(unsigned long, address)
		__field(u64, gva)
		),

	TP_fast_assign(
		__entry->address = address;
		__entry->gva = gva;
		),

	TP_printk("gva %#llx address %#lx",  __entry->gva,
		  __entry->address)
);

#endif

#endif /* _TRACE_KVM_MAIN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
