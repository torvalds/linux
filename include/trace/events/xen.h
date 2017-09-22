#undef TRACE_SYSTEM
#define TRACE_SYSTEM xen

#if !defined(_TRACE_XEN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XEN_H

#include <linux/tracepoint.h>
#include <asm/paravirt_types.h>
#include <asm/xen/trace_types.h>

struct multicall_entry;

/* Multicalls */
DECLARE_EVENT_CLASS(xen_mc__batch,
	    TP_PROTO(enum paravirt_lazy_mode mode),
	    TP_ARGS(mode),
	    TP_STRUCT__entry(
		    __field(enum paravirt_lazy_mode, mode)
		    ),
	    TP_fast_assign(__entry->mode = mode),
	    TP_printk("start batch LAZY_%s",
		      (__entry->mode == PARAVIRT_LAZY_MMU) ? "MMU" :
		      (__entry->mode == PARAVIRT_LAZY_CPU) ? "CPU" : "NONE")
	);
#define DEFINE_XEN_MC_BATCH(name)			\
	DEFINE_EVENT(xen_mc__batch, name,		\
		TP_PROTO(enum paravirt_lazy_mode mode),	\
		     TP_ARGS(mode))

DEFINE_XEN_MC_BATCH(xen_mc_batch);
DEFINE_XEN_MC_BATCH(xen_mc_issue);

TRACE_DEFINE_SIZEOF(ulong);

TRACE_EVENT(xen_mc_entry,
	    TP_PROTO(struct multicall_entry *mc, unsigned nargs),
	    TP_ARGS(mc, nargs),
	    TP_STRUCT__entry(
		    __field(unsigned int, op)
		    __field(unsigned int, nargs)
		    __array(unsigned long, args, 6)
		    ),
	    TP_fast_assign(__entry->op = mc->op;
			   __entry->nargs = nargs;
			   memcpy(__entry->args, mc->args, sizeof(ulong) * nargs);
			   memset(__entry->args + nargs, 0, sizeof(ulong) * (6 - nargs));
		    ),
	    TP_printk("op %u%s args [%lx, %lx, %lx, %lx, %lx, %lx]",
		      __entry->op, xen_hypercall_name(__entry->op),
		      __entry->args[0], __entry->args[1], __entry->args[2],
		      __entry->args[3], __entry->args[4], __entry->args[5])
	);

TRACE_EVENT(xen_mc_entry_alloc,
	    TP_PROTO(size_t args),
	    TP_ARGS(args),
	    TP_STRUCT__entry(
		    __field(size_t, args)
		    ),
	    TP_fast_assign(__entry->args = args),
	    TP_printk("alloc entry %zu arg bytes", __entry->args)
	);

TRACE_EVENT(xen_mc_callback,
	    TP_PROTO(xen_mc_callback_fn_t fn, void *data),
	    TP_ARGS(fn, data),
	    TP_STRUCT__entry(
		    __field(xen_mc_callback_fn_t, fn)
		    __field(void *, data)
		    ),
	    TP_fast_assign(
		    __entry->fn = fn;
		    __entry->data = data;
		    ),
	    TP_printk("callback %pf, data %p",
		      __entry->fn, __entry->data)
	);

TRACE_EVENT(xen_mc_flush_reason,
	    TP_PROTO(enum xen_mc_flush_reason reason),
	    TP_ARGS(reason),
	    TP_STRUCT__entry(
		    __field(enum xen_mc_flush_reason, reason)
		    ),
	    TP_fast_assign(__entry->reason = reason),
	    TP_printk("flush reason %s",
		      (__entry->reason == XEN_MC_FL_NONE) ? "NONE" :
		      (__entry->reason == XEN_MC_FL_BATCH) ? "BATCH" :
		      (__entry->reason == XEN_MC_FL_ARGS) ? "ARGS" :
		      (__entry->reason == XEN_MC_FL_CALLBACK) ? "CALLBACK" : "??")
	);

TRACE_EVENT(xen_mc_flush,
	    TP_PROTO(unsigned mcidx, unsigned argidx, unsigned cbidx),
	    TP_ARGS(mcidx, argidx, cbidx),
	    TP_STRUCT__entry(
		    __field(unsigned, mcidx)
		    __field(unsigned, argidx)
		    __field(unsigned, cbidx)
		    ),
	    TP_fast_assign(__entry->mcidx = mcidx;
			   __entry->argidx = argidx;
			   __entry->cbidx = cbidx),
	    TP_printk("flushing %u hypercalls, %u arg bytes, %u callbacks",
		      __entry->mcidx, __entry->argidx, __entry->cbidx)
	);

TRACE_EVENT(xen_mc_extend_args,
	    TP_PROTO(unsigned long op, size_t args, enum xen_mc_extend_args res),
	    TP_ARGS(op, args, res),
	    TP_STRUCT__entry(
		    __field(unsigned int, op)
		    __field(size_t, args)
		    __field(enum xen_mc_extend_args, res)
		    ),
	    TP_fast_assign(__entry->op = op;
			   __entry->args = args;
			   __entry->res = res),
	    TP_printk("extending op %u%s by %zu bytes res %s",
		      __entry->op, xen_hypercall_name(__entry->op),
		      __entry->args,
		      __entry->res == XEN_MC_XE_OK ? "OK" :
		      __entry->res == XEN_MC_XE_BAD_OP ? "BAD_OP" :
		      __entry->res == XEN_MC_XE_NO_SPACE ? "NO_SPACE" : "???")
	);

TRACE_DEFINE_SIZEOF(pteval_t);
/* mmu */
DECLARE_EVENT_CLASS(xen_mmu__set_pte,
	    TP_PROTO(pte_t *ptep, pte_t pteval),
	    TP_ARGS(ptep, pteval),
	    TP_STRUCT__entry(
		    __field(pte_t *, ptep)
		    __field(pteval_t, pteval)
		    ),
	    TP_fast_assign(__entry->ptep = ptep;
			   __entry->pteval = pteval.pte),
	    TP_printk("ptep %p pteval %0*llx (raw %0*llx)",
		      __entry->ptep,
		      (int)sizeof(pteval_t) * 2, (unsigned long long)pte_val(native_make_pte(__entry->pteval)),
		      (int)sizeof(pteval_t) * 2, (unsigned long long)__entry->pteval)
	);

#define DEFINE_XEN_MMU_SET_PTE(name)				\
	DEFINE_EVENT(xen_mmu__set_pte, name,			\
		     TP_PROTO(pte_t *ptep, pte_t pteval),	\
		     TP_ARGS(ptep, pteval))

DEFINE_XEN_MMU_SET_PTE(xen_mmu_set_pte);
DEFINE_XEN_MMU_SET_PTE(xen_mmu_set_pte_atomic);

TRACE_EVENT(xen_mmu_set_pte_at,
	    TP_PROTO(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pteval),
	    TP_ARGS(mm, addr, ptep, pteval),
	    TP_STRUCT__entry(
		    __field(struct mm_struct *, mm)
		    __field(unsigned long, addr)
		    __field(pte_t *, ptep)
		    __field(pteval_t, pteval)
		    ),
	    TP_fast_assign(__entry->mm = mm;
			   __entry->addr = addr;
			   __entry->ptep = ptep;
			   __entry->pteval = pteval.pte),
	    TP_printk("mm %p addr %lx ptep %p pteval %0*llx (raw %0*llx)",
		      __entry->mm, __entry->addr, __entry->ptep,
		      (int)sizeof(pteval_t) * 2, (unsigned long long)pte_val(native_make_pte(__entry->pteval)),
		      (int)sizeof(pteval_t) * 2, (unsigned long long)__entry->pteval)
	);

TRACE_EVENT(xen_mmu_pte_clear,
	    TP_PROTO(struct mm_struct *mm, unsigned long addr, pte_t *ptep),
	    TP_ARGS(mm, addr, ptep),
	    TP_STRUCT__entry(
		    __field(struct mm_struct *, mm)
		    __field(unsigned long, addr)
		    __field(pte_t *, ptep)
		    ),
	    TP_fast_assign(__entry->mm = mm;
			   __entry->addr = addr;
			   __entry->ptep = ptep),
	    TP_printk("mm %p addr %lx ptep %p",
		      __entry->mm, __entry->addr, __entry->ptep)
	);

TRACE_DEFINE_SIZEOF(pmdval_t);

TRACE_EVENT(xen_mmu_set_pmd,
	    TP_PROTO(pmd_t *pmdp, pmd_t pmdval),
	    TP_ARGS(pmdp, pmdval),
	    TP_STRUCT__entry(
		    __field(pmd_t *, pmdp)
		    __field(pmdval_t, pmdval)
		    ),
	    TP_fast_assign(__entry->pmdp = pmdp;
			   __entry->pmdval = pmdval.pmd),
	    TP_printk("pmdp %p pmdval %0*llx (raw %0*llx)",
		      __entry->pmdp,
		      (int)sizeof(pmdval_t) * 2, (unsigned long long)pmd_val(native_make_pmd(__entry->pmdval)),
		      (int)sizeof(pmdval_t) * 2, (unsigned long long)__entry->pmdval)
	);

TRACE_EVENT(xen_mmu_pmd_clear,
	    TP_PROTO(pmd_t *pmdp),
	    TP_ARGS(pmdp),
	    TP_STRUCT__entry(
		    __field(pmd_t *, pmdp)
		    ),
	    TP_fast_assign(__entry->pmdp = pmdp),
	    TP_printk("pmdp %p", __entry->pmdp)
	);

#if CONFIG_PGTABLE_LEVELS >= 4

TRACE_DEFINE_SIZEOF(pudval_t);

TRACE_EVENT(xen_mmu_set_pud,
	    TP_PROTO(pud_t *pudp, pud_t pudval),
	    TP_ARGS(pudp, pudval),
	    TP_STRUCT__entry(
		    __field(pud_t *, pudp)
		    __field(pudval_t, pudval)
		    ),
	    TP_fast_assign(__entry->pudp = pudp;
			   __entry->pudval = native_pud_val(pudval)),
	    TP_printk("pudp %p pudval %0*llx (raw %0*llx)",
		      __entry->pudp,
		      (int)sizeof(pudval_t) * 2, (unsigned long long)pud_val(native_make_pud(__entry->pudval)),
		      (int)sizeof(pudval_t) * 2, (unsigned long long)__entry->pudval)
	);

TRACE_DEFINE_SIZEOF(p4dval_t);

TRACE_EVENT(xen_mmu_set_p4d,
	    TP_PROTO(p4d_t *p4dp, p4d_t *user_p4dp, p4d_t p4dval),
	    TP_ARGS(p4dp, user_p4dp, p4dval),
	    TP_STRUCT__entry(
		    __field(p4d_t *, p4dp)
		    __field(p4d_t *, user_p4dp)
		    __field(p4dval_t, p4dval)
		    ),
	    TP_fast_assign(__entry->p4dp = p4dp;
			   __entry->user_p4dp = user_p4dp;
			   __entry->p4dval = p4d_val(p4dval)),
	    TP_printk("p4dp %p user_p4dp %p p4dval %0*llx (raw %0*llx)",
		      __entry->p4dp, __entry->user_p4dp,
		      (int)sizeof(p4dval_t) * 2, (unsigned long long)pgd_val(native_make_pgd(__entry->p4dval)),
		      (int)sizeof(p4dval_t) * 2, (unsigned long long)__entry->p4dval)
	);
#else

TRACE_EVENT(xen_mmu_set_pud,
	    TP_PROTO(pud_t *pudp, pud_t pudval),
	    TP_ARGS(pudp, pudval),
	    TP_STRUCT__entry(
		    __field(pud_t *, pudp)
		    __field(pudval_t, pudval)
		    ),
	    TP_fast_assign(__entry->pudp = pudp;
			   __entry->pudval = native_pud_val(pudval)),
	    TP_printk("pudp %p pudval %0*llx (raw %0*llx)",
		      __entry->pudp,
		      (int)sizeof(pudval_t) * 2, (unsigned long long)pgd_val(native_make_pgd(__entry->pudval)),
		      (int)sizeof(pudval_t) * 2, (unsigned long long)__entry->pudval)
	);

#endif

DECLARE_EVENT_CLASS(xen_mmu_ptep_modify_prot,
	    TP_PROTO(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pteval),
	    TP_ARGS(mm, addr, ptep, pteval),
	    TP_STRUCT__entry(
		    __field(struct mm_struct *, mm)
		    __field(unsigned long, addr)
		    __field(pte_t *, ptep)
		    __field(pteval_t, pteval)
		    ),
	    TP_fast_assign(__entry->mm = mm;
			   __entry->addr = addr;
			   __entry->ptep = ptep;
			   __entry->pteval = pteval.pte),
	    TP_printk("mm %p addr %lx ptep %p pteval %0*llx (raw %0*llx)",
		      __entry->mm, __entry->addr, __entry->ptep,
		      (int)sizeof(pteval_t) * 2, (unsigned long long)pte_val(native_make_pte(__entry->pteval)),
		      (int)sizeof(pteval_t) * 2, (unsigned long long)__entry->pteval)
	);
#define DEFINE_XEN_MMU_PTEP_MODIFY_PROT(name)				\
	DEFINE_EVENT(xen_mmu_ptep_modify_prot, name,			\
		     TP_PROTO(struct mm_struct *mm, unsigned long addr,	\
			      pte_t *ptep, pte_t pteval),		\
		     TP_ARGS(mm, addr, ptep, pteval))

DEFINE_XEN_MMU_PTEP_MODIFY_PROT(xen_mmu_ptep_modify_prot_start);
DEFINE_XEN_MMU_PTEP_MODIFY_PROT(xen_mmu_ptep_modify_prot_commit);

TRACE_EVENT(xen_mmu_alloc_ptpage,
	    TP_PROTO(struct mm_struct *mm, unsigned long pfn, unsigned level, bool pinned),
	    TP_ARGS(mm, pfn, level, pinned),
	    TP_STRUCT__entry(
		    __field(struct mm_struct *, mm)
		    __field(unsigned long, pfn)
		    __field(unsigned, level)
		    __field(bool, pinned)
		    ),
	    TP_fast_assign(__entry->mm = mm;
			   __entry->pfn = pfn;
			   __entry->level = level;
			   __entry->pinned = pinned),
	    TP_printk("mm %p  pfn %lx  level %d  %spinned",
		      __entry->mm, __entry->pfn, __entry->level,
		      __entry->pinned ? "" : "un")
	);

TRACE_EVENT(xen_mmu_release_ptpage,
	    TP_PROTO(unsigned long pfn, unsigned level, bool pinned),
	    TP_ARGS(pfn, level, pinned),
	    TP_STRUCT__entry(
		    __field(unsigned long, pfn)
		    __field(unsigned, level)
		    __field(bool, pinned)
		    ),
	    TP_fast_assign(__entry->pfn = pfn;
			   __entry->level = level;
			   __entry->pinned = pinned),
	    TP_printk("pfn %lx  level %d  %spinned",
		      __entry->pfn, __entry->level,
		      __entry->pinned ? "" : "un")
	);

DECLARE_EVENT_CLASS(xen_mmu_pgd,
	    TP_PROTO(struct mm_struct *mm, pgd_t *pgd),
	    TP_ARGS(mm, pgd),
	    TP_STRUCT__entry(
		    __field(struct mm_struct *, mm)
		    __field(pgd_t *, pgd)
		    ),
	    TP_fast_assign(__entry->mm = mm;
			   __entry->pgd = pgd),
	    TP_printk("mm %p pgd %p", __entry->mm, __entry->pgd)
	);
#define DEFINE_XEN_MMU_PGD_EVENT(name)				\
	DEFINE_EVENT(xen_mmu_pgd, name,				\
		TP_PROTO(struct mm_struct *mm, pgd_t *pgd),	\
		     TP_ARGS(mm, pgd))

DEFINE_XEN_MMU_PGD_EVENT(xen_mmu_pgd_pin);
DEFINE_XEN_MMU_PGD_EVENT(xen_mmu_pgd_unpin);

TRACE_EVENT(xen_mmu_flush_tlb_all,
	    TP_PROTO(int x),
	    TP_ARGS(x),
	    TP_STRUCT__entry(__array(char, x, 0)),
	    TP_fast_assign((void)x),
	    TP_printk("%s", "")
	);

TRACE_EVENT(xen_mmu_flush_tlb,
	    TP_PROTO(int x),
	    TP_ARGS(x),
	    TP_STRUCT__entry(__array(char, x, 0)),
	    TP_fast_assign((void)x),
	    TP_printk("%s", "")
	);

TRACE_EVENT(xen_mmu_flush_tlb_single,
	    TP_PROTO(unsigned long addr),
	    TP_ARGS(addr),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    ),
	    TP_fast_assign(__entry->addr = addr),
	    TP_printk("addr %lx", __entry->addr)
	);

TRACE_EVENT(xen_mmu_flush_tlb_others,
	    TP_PROTO(const struct cpumask *cpus, struct mm_struct *mm,
		     unsigned long addr, unsigned long end),
	    TP_ARGS(cpus, mm, addr, end),
	    TP_STRUCT__entry(
		    __field(unsigned, ncpus)
		    __field(struct mm_struct *, mm)
		    __field(unsigned long, addr)
		    __field(unsigned long, end)
		    ),
	    TP_fast_assign(__entry->ncpus = cpumask_weight(cpus);
			   __entry->mm = mm;
			   __entry->addr = addr,
			   __entry->end = end),
	    TP_printk("ncpus %d mm %p addr %lx, end %lx",
		      __entry->ncpus, __entry->mm, __entry->addr, __entry->end)
	);

TRACE_EVENT(xen_mmu_write_cr3,
	    TP_PROTO(bool kernel, unsigned long cr3),
	    TP_ARGS(kernel, cr3),
	    TP_STRUCT__entry(
		    __field(bool, kernel)
		    __field(unsigned long, cr3)
		    ),
	    TP_fast_assign(__entry->kernel = kernel;
			   __entry->cr3 = cr3),
	    TP_printk("%s cr3 %lx",
		      __entry->kernel ? "kernel" : "user", __entry->cr3)
	);


/* CPU */
TRACE_EVENT(xen_cpu_write_ldt_entry,
	    TP_PROTO(struct desc_struct *dt, int entrynum, u64 desc),
	    TP_ARGS(dt, entrynum, desc),
	    TP_STRUCT__entry(
		    __field(struct desc_struct *, dt)
		    __field(int, entrynum)
		    __field(u64, desc)
		    ),
	    TP_fast_assign(__entry->dt = dt;
			   __entry->entrynum = entrynum;
			   __entry->desc = desc;
		    ),
	    TP_printk("dt %p  entrynum %d  entry %016llx",
		      __entry->dt, __entry->entrynum,
		      (unsigned long long)__entry->desc)
	);

TRACE_EVENT(xen_cpu_write_idt_entry,
	    TP_PROTO(gate_desc *dt, int entrynum, const gate_desc *ent),
	    TP_ARGS(dt, entrynum, ent),
	    TP_STRUCT__entry(
		    __field(gate_desc *, dt)
		    __field(int, entrynum)
		    ),
	    TP_fast_assign(__entry->dt = dt;
			   __entry->entrynum = entrynum;
		    ),
	    TP_printk("dt %p  entrynum %d",
		      __entry->dt, __entry->entrynum)
	);

TRACE_EVENT(xen_cpu_load_idt,
	    TP_PROTO(const struct desc_ptr *desc),
	    TP_ARGS(desc),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    ),
	    TP_fast_assign(__entry->addr = desc->address),
	    TP_printk("addr %lx", __entry->addr)
	);

TRACE_EVENT(xen_cpu_write_gdt_entry,
	    TP_PROTO(struct desc_struct *dt, int entrynum, const void *desc, int type),
	    TP_ARGS(dt, entrynum, desc, type),
	    TP_STRUCT__entry(
		    __field(u64, desc)
		    __field(struct desc_struct *, dt)
		    __field(int, entrynum)
		    __field(int, type)
		    ),
	    TP_fast_assign(__entry->dt = dt;
			   __entry->entrynum = entrynum;
			   __entry->desc = *(u64 *)desc;
			   __entry->type = type;
		    ),
	    TP_printk("dt %p  entrynum %d  type %d  desc %016llx",
		      __entry->dt, __entry->entrynum, __entry->type,
		      (unsigned long long)__entry->desc)
	);

TRACE_EVENT(xen_cpu_set_ldt,
	    TP_PROTO(const void *addr, unsigned entries),
	    TP_ARGS(addr, entries),
	    TP_STRUCT__entry(
		    __field(const void *, addr)
		    __field(unsigned, entries)
		    ),
	    TP_fast_assign(__entry->addr = addr;
			   __entry->entries = entries),
	    TP_printk("addr %p  entries %u",
		      __entry->addr, __entry->entries)
	);


#endif /*  _TRACE_XEN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
