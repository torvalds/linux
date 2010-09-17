/*
 * Helper macros to support writing architecture specific
 * linker scripts.
 *
 * A minimal linker scripts has following content:
 * [This is a sample, architectures may have special requiriements]
 *
 * OUTPUT_FORMAT(...)
 * OUTPUT_ARCH(...)
 * ENTRY(...)
 * SECTIONS
 * {
 *	. = START;
 *	__init_begin = .;
 *	HEAD_TEXT_SECTION
 *	INIT_TEXT_SECTION(PAGE_SIZE)
 *	INIT_DATA_SECTION(...)
 *	PERCPU(PAGE_SIZE)
 *	__init_end = .;
 *
 *	_stext = .;
 *	TEXT_SECTION = 0
 *	_etext = .;
 *
 *      _sdata = .;
 *	RO_DATA_SECTION(PAGE_SIZE)
 *	RW_DATA_SECTION(...)
 *	_edata = .;
 *
 *	EXCEPTION_TABLE(...)
 *	NOTES
 *
 *	BSS_SECTION(0, 0, 0)
 *	_end = .;
 *
 *	STABS_DEBUG
 *	DWARF_DEBUG
 *
 *	DISCARDS		// must be the last
 * }
 *
 * [__init_begin, __init_end] is the init section that may be freed after init
 * [_stext, _etext] is the text section
 * [_sdata, _edata] is the data section
 *
 * Some of the included output section have their own set of constants.
 * Examples are: [__initramfs_start, __initramfs_end] for initramfs and
 *               [__nosave_begin, __nosave_end] for the nosave data
 */

#ifndef LOAD_OFFSET
#define LOAD_OFFSET 0
#endif

#ifndef SYMBOL_PREFIX
#define VMLINUX_SYMBOL(sym) sym
#else
#define PASTE2(x,y) x##y
#define PASTE(x,y) PASTE2(x,y)
#define VMLINUX_SYMBOL(sym) PASTE(SYMBOL_PREFIX, sym)
#endif

/* Align . to a 8 byte boundary equals to maximum function alignment. */
#define ALIGN_FUNCTION()  . = ALIGN(8)

/*
 * Align to a 32 byte boundary equal to the
 * alignment gcc 4.5 uses for a struct
 */
#define STRUCT_ALIGN() . = ALIGN(32)

/* The actual configuration determine if the init/exit sections
 * are handled as text/data or they can be discarded (which
 * often happens at runtime)
 */
#ifdef CONFIG_HOTPLUG
#define DEV_KEEP(sec)    *(.dev##sec)
#define DEV_DISCARD(sec)
#else
#define DEV_KEEP(sec)
#define DEV_DISCARD(sec) *(.dev##sec)
#endif

#ifdef CONFIG_HOTPLUG_CPU
#define CPU_KEEP(sec)    *(.cpu##sec)
#define CPU_DISCARD(sec)
#else
#define CPU_KEEP(sec)
#define CPU_DISCARD(sec) *(.cpu##sec)
#endif

#if defined(CONFIG_MEMORY_HOTPLUG)
#define MEM_KEEP(sec)    *(.mem##sec)
#define MEM_DISCARD(sec)
#else
#define MEM_KEEP(sec)
#define MEM_DISCARD(sec) *(.mem##sec)
#endif

#ifdef CONFIG_FTRACE_MCOUNT_RECORD
#define MCOUNT_REC()	. = ALIGN(8);				\
			VMLINUX_SYMBOL(__start_mcount_loc) = .; \
			*(__mcount_loc)				\
			VMLINUX_SYMBOL(__stop_mcount_loc) = .;
#else
#define MCOUNT_REC()
#endif

#ifdef CONFIG_TRACE_BRANCH_PROFILING
#define LIKELY_PROFILE()	VMLINUX_SYMBOL(__start_annotated_branch_profile) = .; \
				*(_ftrace_annotated_branch)			      \
				VMLINUX_SYMBOL(__stop_annotated_branch_profile) = .;
#else
#define LIKELY_PROFILE()
#endif

#ifdef CONFIG_PROFILE_ALL_BRANCHES
#define BRANCH_PROFILE()	VMLINUX_SYMBOL(__start_branch_profile) = .;   \
				*(_ftrace_branch)			      \
				VMLINUX_SYMBOL(__stop_branch_profile) = .;
#else
#define BRANCH_PROFILE()
#endif

#ifdef CONFIG_EVENT_TRACING
#define FTRACE_EVENTS()	VMLINUX_SYMBOL(__start_ftrace_events) = .;	\
			*(_ftrace_events)				\
			VMLINUX_SYMBOL(__stop_ftrace_events) = .;
#else
#define FTRACE_EVENTS()
#endif

#ifdef CONFIG_TRACING
#define TRACE_PRINTKS() VMLINUX_SYMBOL(__start___trace_bprintk_fmt) = .;      \
			 *(__trace_printk_fmt) /* Trace_printk fmt' pointer */ \
			 VMLINUX_SYMBOL(__stop___trace_bprintk_fmt) = .;
#else
#define TRACE_PRINTKS()
#endif

#ifdef CONFIG_FTRACE_SYSCALLS
#define TRACE_SYSCALLS() VMLINUX_SYMBOL(__start_syscalls_metadata) = .;	\
			 *(__syscalls_metadata)				\
			 VMLINUX_SYMBOL(__stop_syscalls_metadata) = .;
#else
#define TRACE_SYSCALLS()
#endif

/* .data section */
#define DATA_DATA							\
	*(.data)							\
	*(.ref.data)							\
	DEV_KEEP(init.data)						\
	DEV_KEEP(exit.data)						\
	CPU_KEEP(init.data)						\
	CPU_KEEP(exit.data)						\
	MEM_KEEP(init.data)						\
	MEM_KEEP(exit.data)						\
	. = ALIGN(8);							\
	VMLINUX_SYMBOL(__start___markers) = .;				\
	*(__markers)							\
	VMLINUX_SYMBOL(__stop___markers) = .;				\
	. = ALIGN(32);							\
	VMLINUX_SYMBOL(__start___tracepoints) = .;			\
	*(__tracepoints)						\
	VMLINUX_SYMBOL(__stop___tracepoints) = .;			\
	/* implement dynamic printk debug */				\
	. = ALIGN(8);							\
	VMLINUX_SYMBOL(__start___verbose) = .;                          \
	*(__verbose)                                                    \
	VMLINUX_SYMBOL(__stop___verbose) = .;				\
	LIKELY_PROFILE()		       				\
	BRANCH_PROFILE()						\
	TRACE_PRINTKS()							\
									\
	STRUCT_ALIGN();							\
	FTRACE_EVENTS()							\
									\
	STRUCT_ALIGN();							\
	TRACE_SYSCALLS()

/*
 * Data section helpers
 */
#define NOSAVE_DATA							\
	. = ALIGN(PAGE_SIZE);						\
	VMLINUX_SYMBOL(__nosave_begin) = .;				\
	*(.data..nosave)						\
	. = ALIGN(PAGE_SIZE);						\
	VMLINUX_SYMBOL(__nosave_end) = .;

#define PAGE_ALIGNED_DATA(page_align)					\
	. = ALIGN(page_align);						\
	*(.data..page_aligned)

#define READ_MOSTLY_DATA(align)						\
	. = ALIGN(align);						\
	*(.data..read_mostly)

#define CACHELINE_ALIGNED_DATA(align)					\
	. = ALIGN(align);						\
	*(.data..cacheline_aligned)

#define INIT_TASK_DATA(align)						\
	. = ALIGN(align);						\
	*(.data..init_task)

/*
 * Read only Data
 */
#define RO_DATA_SECTION(align)						\
	. = ALIGN((align));						\
	.rodata           : AT(ADDR(.rodata) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__start_rodata) = .;			\
		*(.rodata) *(.rodata.*)					\
		*(__vermagic)		/* Kernel version magic */	\
		*(__markers_strings)	/* Markers: strings */		\
		*(__tracepoints_strings)/* Tracepoints: strings */	\
	}								\
									\
	.rodata1          : AT(ADDR(.rodata1) - LOAD_OFFSET) {		\
		*(.rodata1)						\
	}								\
									\
	BUG_TABLE							\
									\
	/* PCI quirks */						\
	.pci_fixup        : AT(ADDR(.pci_fixup) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__start_pci_fixups_early) = .;		\
		*(.pci_fixup_early)					\
		VMLINUX_SYMBOL(__end_pci_fixups_early) = .;		\
		VMLINUX_SYMBOL(__start_pci_fixups_header) = .;		\
		*(.pci_fixup_header)					\
		VMLINUX_SYMBOL(__end_pci_fixups_header) = .;		\
		VMLINUX_SYMBOL(__start_pci_fixups_final) = .;		\
		*(.pci_fixup_final)					\
		VMLINUX_SYMBOL(__end_pci_fixups_final) = .;		\
		VMLINUX_SYMBOL(__start_pci_fixups_enable) = .;		\
		*(.pci_fixup_enable)					\
		VMLINUX_SYMBOL(__end_pci_fixups_enable) = .;		\
		VMLINUX_SYMBOL(__start_pci_fixups_resume) = .;		\
		*(.pci_fixup_resume)					\
		VMLINUX_SYMBOL(__end_pci_fixups_resume) = .;		\
		VMLINUX_SYMBOL(__start_pci_fixups_resume_early) = .;	\
		*(.pci_fixup_resume_early)				\
		VMLINUX_SYMBOL(__end_pci_fixups_resume_early) = .;	\
		VMLINUX_SYMBOL(__start_pci_fixups_suspend) = .;		\
		*(.pci_fixup_suspend)					\
		VMLINUX_SYMBOL(__end_pci_fixups_suspend) = .;		\
	}								\
									\
	/* Built-in firmware blobs */					\
	.builtin_fw        : AT(ADDR(.builtin_fw) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__start_builtin_fw) = .;			\
		*(.builtin_fw)						\
		VMLINUX_SYMBOL(__end_builtin_fw) = .;			\
	}								\
									\
	/* RapidIO route ops */						\
	.rio_ops        : AT(ADDR(.rio_ops) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__start_rio_switch_ops) = .;		\
		*(.rio_switch_ops)					\
		VMLINUX_SYMBOL(__end_rio_switch_ops) = .;		\
	}								\
									\
	TRACEDATA							\
									\
	/* Kernel symbol table: Normal symbols */			\
	__ksymtab         : AT(ADDR(__ksymtab) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__start___ksymtab) = .;			\
		*(__ksymtab)						\
		VMLINUX_SYMBOL(__stop___ksymtab) = .;			\
	}								\
									\
	/* Kernel symbol table: GPL-only symbols */			\
	__ksymtab_gpl     : AT(ADDR(__ksymtab_gpl) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__start___ksymtab_gpl) = .;		\
		*(__ksymtab_gpl)					\
		VMLINUX_SYMBOL(__stop___ksymtab_gpl) = .;		\
	}								\
									\
	/* Kernel symbol table: Normal unused symbols */		\
	__ksymtab_unused  : AT(ADDR(__ksymtab_unused) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__start___ksymtab_unused) = .;		\
		*(__ksymtab_unused)					\
		VMLINUX_SYMBOL(__stop___ksymtab_unused) = .;		\
	}								\
									\
	/* Kernel symbol table: GPL-only unused symbols */		\
	__ksymtab_unused_gpl : AT(ADDR(__ksymtab_unused_gpl) - LOAD_OFFSET) { \
		VMLINUX_SYMBOL(__start___ksymtab_unused_gpl) = .;	\
		*(__ksymtab_unused_gpl)					\
		VMLINUX_SYMBOL(__stop___ksymtab_unused_gpl) = .;	\
	}								\
									\
	/* Kernel symbol table: GPL-future-only symbols */		\
	__ksymtab_gpl_future : AT(ADDR(__ksymtab_gpl_future) - LOAD_OFFSET) { \
		VMLINUX_SYMBOL(__start___ksymtab_gpl_future) = .;	\
		*(__ksymtab_gpl_future)					\
		VMLINUX_SYMBOL(__stop___ksymtab_gpl_future) = .;	\
	}								\
									\
	/* Kernel symbol table: Normal symbols */			\
	__kcrctab         : AT(ADDR(__kcrctab) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__start___kcrctab) = .;			\
		*(__kcrctab)						\
		VMLINUX_SYMBOL(__stop___kcrctab) = .;			\
	}								\
									\
	/* Kernel symbol table: GPL-only symbols */			\
	__kcrctab_gpl     : AT(ADDR(__kcrctab_gpl) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__start___kcrctab_gpl) = .;		\
		*(__kcrctab_gpl)					\
		VMLINUX_SYMBOL(__stop___kcrctab_gpl) = .;		\
	}								\
									\
	/* Kernel symbol table: Normal unused symbols */		\
	__kcrctab_unused  : AT(ADDR(__kcrctab_unused) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__start___kcrctab_unused) = .;		\
		*(__kcrctab_unused)					\
		VMLINUX_SYMBOL(__stop___kcrctab_unused) = .;		\
	}								\
									\
	/* Kernel symbol table: GPL-only unused symbols */		\
	__kcrctab_unused_gpl : AT(ADDR(__kcrctab_unused_gpl) - LOAD_OFFSET) { \
		VMLINUX_SYMBOL(__start___kcrctab_unused_gpl) = .;	\
		*(__kcrctab_unused_gpl)					\
		VMLINUX_SYMBOL(__stop___kcrctab_unused_gpl) = .;	\
	}								\
									\
	/* Kernel symbol table: GPL-future-only symbols */		\
	__kcrctab_gpl_future : AT(ADDR(__kcrctab_gpl_future) - LOAD_OFFSET) { \
		VMLINUX_SYMBOL(__start___kcrctab_gpl_future) = .;	\
		*(__kcrctab_gpl_future)					\
		VMLINUX_SYMBOL(__stop___kcrctab_gpl_future) = .;	\
	}								\
									\
	/* Kernel symbol table: strings */				\
        __ksymtab_strings : AT(ADDR(__ksymtab_strings) - LOAD_OFFSET) {	\
		*(__ksymtab_strings)					\
	}								\
									\
	/* __*init sections */						\
	__init_rodata : AT(ADDR(__init_rodata) - LOAD_OFFSET) {		\
		*(.ref.rodata)						\
		DEV_KEEP(init.rodata)					\
		DEV_KEEP(exit.rodata)					\
		CPU_KEEP(init.rodata)					\
		CPU_KEEP(exit.rodata)					\
		MEM_KEEP(init.rodata)					\
		MEM_KEEP(exit.rodata)					\
	}								\
									\
	/* Built-in module parameters. */				\
	__param : AT(ADDR(__param) - LOAD_OFFSET) {			\
		VMLINUX_SYMBOL(__start___param) = .;			\
		*(__param)						\
		VMLINUX_SYMBOL(__stop___param) = .;			\
		. = ALIGN((align));					\
		VMLINUX_SYMBOL(__end_rodata) = .;			\
	}								\
	. = ALIGN((align));

/* RODATA & RO_DATA provided for backward compatibility.
 * All archs are supposed to use RO_DATA() */
#define RODATA          RO_DATA_SECTION(4096)
#define RO_DATA(align)  RO_DATA_SECTION(align)

#define SECURITY_INIT							\
	.security_initcall.init : AT(ADDR(.security_initcall.init) - LOAD_OFFSET) { \
		VMLINUX_SYMBOL(__security_initcall_start) = .;		\
		*(.security_initcall.init) 				\
		VMLINUX_SYMBOL(__security_initcall_end) = .;		\
	}

/* .text section. Map to function alignment to avoid address changes
 * during second ld run in second ld pass when generating System.map */
#define TEXT_TEXT							\
		ALIGN_FUNCTION();					\
		*(.text.hot)						\
		*(.text)						\
		*(.ref.text)						\
	DEV_KEEP(init.text)						\
	DEV_KEEP(exit.text)						\
	CPU_KEEP(init.text)						\
	CPU_KEEP(exit.text)						\
	MEM_KEEP(init.text)						\
	MEM_KEEP(exit.text)						\
		*(.text.unlikely)


/* sched.text is aling to function alignment to secure we have same
 * address even at second ld pass when generating System.map */
#define SCHED_TEXT							\
		ALIGN_FUNCTION();					\
		VMLINUX_SYMBOL(__sched_text_start) = .;			\
		*(.sched.text)						\
		VMLINUX_SYMBOL(__sched_text_end) = .;

/* spinlock.text is aling to function alignment to secure we have same
 * address even at second ld pass when generating System.map */
#define LOCK_TEXT							\
		ALIGN_FUNCTION();					\
		VMLINUX_SYMBOL(__lock_text_start) = .;			\
		*(.spinlock.text)					\
		VMLINUX_SYMBOL(__lock_text_end) = .;

#define KPROBES_TEXT							\
		ALIGN_FUNCTION();					\
		VMLINUX_SYMBOL(__kprobes_text_start) = .;		\
		*(.kprobes.text)					\
		VMLINUX_SYMBOL(__kprobes_text_end) = .;

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
#define IRQENTRY_TEXT							\
		ALIGN_FUNCTION();					\
		VMLINUX_SYMBOL(__irqentry_text_start) = .;		\
		*(.irqentry.text)					\
		VMLINUX_SYMBOL(__irqentry_text_end) = .;
#else
#define IRQENTRY_TEXT
#endif

/* Section used for early init (in .S files) */
#define HEAD_TEXT  *(.head.text)

#define HEAD_TEXT_SECTION							\
	.head.text : AT(ADDR(.head.text) - LOAD_OFFSET) {		\
		HEAD_TEXT						\
	}

/*
 * Exception table
 */
#define EXCEPTION_TABLE(align)						\
	. = ALIGN(align);						\
	__ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__start___ex_table) = .;			\
		*(__ex_table)						\
		VMLINUX_SYMBOL(__stop___ex_table) = .;			\
	}

/*
 * Init task
 */
#define INIT_TASK_DATA_SECTION(align)					\
	. = ALIGN(align);						\
	.data..init_task :  AT(ADDR(.data..init_task) - LOAD_OFFSET) {	\
		INIT_TASK_DATA(align)					\
	}

#ifdef CONFIG_CONSTRUCTORS
#define KERNEL_CTORS()	. = ALIGN(8);			   \
			VMLINUX_SYMBOL(__ctors_start) = .; \
			*(.ctors)			   \
			VMLINUX_SYMBOL(__ctors_end) = .;
#else
#define KERNEL_CTORS()
#endif

/* init and exit section handling */
#define INIT_DATA							\
	*(.init.data)							\
	DEV_DISCARD(init.data)						\
	CPU_DISCARD(init.data)						\
	MEM_DISCARD(init.data)						\
	KERNEL_CTORS()							\
	*(.init.rodata)							\
	MCOUNT_REC()							\
	DEV_DISCARD(init.rodata)					\
	CPU_DISCARD(init.rodata)					\
	MEM_DISCARD(init.rodata)

#define INIT_TEXT							\
	*(.init.text)							\
	DEV_DISCARD(init.text)						\
	CPU_DISCARD(init.text)						\
	MEM_DISCARD(init.text)

#define EXIT_DATA							\
	*(.exit.data)							\
	DEV_DISCARD(exit.data)						\
	DEV_DISCARD(exit.rodata)					\
	CPU_DISCARD(exit.data)						\
	CPU_DISCARD(exit.rodata)					\
	MEM_DISCARD(exit.data)						\
	MEM_DISCARD(exit.rodata)

#define EXIT_TEXT							\
	*(.exit.text)							\
	DEV_DISCARD(exit.text)						\
	CPU_DISCARD(exit.text)						\
	MEM_DISCARD(exit.text)

#define EXIT_CALL							\
	*(.exitcall.exit)

/*
 * bss (Block Started by Symbol) - uninitialized data
 * zeroed during startup
 */
#define SBSS(sbss_align)						\
	. = ALIGN(sbss_align);						\
	.sbss : AT(ADDR(.sbss) - LOAD_OFFSET) {				\
		*(.sbss)						\
		*(.scommon)						\
	}

#define BSS(bss_align)							\
	. = ALIGN(bss_align);						\
	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {				\
		*(.bss..page_aligned)					\
		*(.dynbss)						\
		*(.bss)							\
		*(COMMON)						\
	}

/*
 * DWARF debug sections.
 * Symbols in the DWARF debugging sections are relative to
 * the beginning of the section so we begin them at 0.
 */
#define DWARF_DEBUG							\
		/* DWARF 1 */						\
		.debug          0 : { *(.debug) }			\
		.line           0 : { *(.line) }			\
		/* GNU DWARF 1 extensions */				\
		.debug_srcinfo  0 : { *(.debug_srcinfo) }		\
		.debug_sfnames  0 : { *(.debug_sfnames) }		\
		/* DWARF 1.1 and DWARF 2 */				\
		.debug_aranges  0 : { *(.debug_aranges) }		\
		.debug_pubnames 0 : { *(.debug_pubnames) }		\
		/* DWARF 2 */						\
		.debug_info     0 : { *(.debug_info			\
				.gnu.linkonce.wi.*) }			\
		.debug_abbrev   0 : { *(.debug_abbrev) }		\
		.debug_line     0 : { *(.debug_line) }			\
		.debug_frame    0 : { *(.debug_frame) }			\
		.debug_str      0 : { *(.debug_str) }			\
		.debug_loc      0 : { *(.debug_loc) }			\
		.debug_macinfo  0 : { *(.debug_macinfo) }		\
		/* SGI/MIPS DWARF 2 extensions */			\
		.debug_weaknames 0 : { *(.debug_weaknames) }		\
		.debug_funcnames 0 : { *(.debug_funcnames) }		\
		.debug_typenames 0 : { *(.debug_typenames) }		\
		.debug_varnames  0 : { *(.debug_varnames) }		\

		/* Stabs debugging sections.  */
#define STABS_DEBUG							\
		.stab 0 : { *(.stab) }					\
		.stabstr 0 : { *(.stabstr) }				\
		.stab.excl 0 : { *(.stab.excl) }			\
		.stab.exclstr 0 : { *(.stab.exclstr) }			\
		.stab.index 0 : { *(.stab.index) }			\
		.stab.indexstr 0 : { *(.stab.indexstr) }		\
		.comment 0 : { *(.comment) }

#ifdef CONFIG_GENERIC_BUG
#define BUG_TABLE							\
	. = ALIGN(8);							\
	__bug_table : AT(ADDR(__bug_table) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__start___bug_table) = .;		\
		*(__bug_table)						\
		VMLINUX_SYMBOL(__stop___bug_table) = .;			\
	}
#else
#define BUG_TABLE
#endif

#ifdef CONFIG_PM_TRACE
#define TRACEDATA							\
	. = ALIGN(4);							\
	.tracedata : AT(ADDR(.tracedata) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__tracedata_start) = .;			\
		*(.tracedata)						\
		VMLINUX_SYMBOL(__tracedata_end) = .;			\
	}
#else
#define TRACEDATA
#endif

#define NOTES								\
	.notes : AT(ADDR(.notes) - LOAD_OFFSET) {			\
		VMLINUX_SYMBOL(__start_notes) = .;			\
		*(.note.*)						\
		VMLINUX_SYMBOL(__stop_notes) = .;			\
	}

#define INIT_SETUP(initsetup_align)					\
		. = ALIGN(initsetup_align);				\
		VMLINUX_SYMBOL(__setup_start) = .;			\
		*(.init.setup)						\
		VMLINUX_SYMBOL(__setup_end) = .;

#define INITCALLS							\
	*(.initcallearly.init)						\
	VMLINUX_SYMBOL(__early_initcall_end) = .;			\
  	*(.initcall0.init)						\
  	*(.initcall0s.init)						\
  	*(.initcall1.init)						\
  	*(.initcall1s.init)						\
  	*(.initcall2.init)						\
  	*(.initcall2s.init)						\
  	*(.initcall3.init)						\
  	*(.initcall3s.init)						\
  	*(.initcall4.init)						\
  	*(.initcall4s.init)						\
  	*(.initcall5.init)						\
  	*(.initcall5s.init)						\
	*(.initcallrootfs.init)						\
  	*(.initcall6.init)						\
  	*(.initcall6s.init)						\
  	*(.initcall7.init)						\
  	*(.initcall7s.init)

#define INIT_CALLS							\
		VMLINUX_SYMBOL(__initcall_start) = .;			\
		INITCALLS						\
		VMLINUX_SYMBOL(__initcall_end) = .;

#define CON_INITCALL							\
		VMLINUX_SYMBOL(__con_initcall_start) = .;		\
		*(.con_initcall.init)					\
		VMLINUX_SYMBOL(__con_initcall_end) = .;

#define SECURITY_INITCALL						\
		VMLINUX_SYMBOL(__security_initcall_start) = .;		\
		*(.security_initcall.init)				\
		VMLINUX_SYMBOL(__security_initcall_end) = .;

#ifdef CONFIG_BLK_DEV_INITRD
#define INIT_RAM_FS							\
	. = ALIGN(PAGE_SIZE);						\
	VMLINUX_SYMBOL(__initramfs_start) = .;				\
	*(.init.ramfs)							\
	. = ALIGN(8);							\
	*(.init.ramfs.info)
#else
#define INIT_RAM_FS
#endif

/*
 * Default discarded sections.
 *
 * Some archs want to discard exit text/data at runtime rather than
 * link time due to cross-section references such as alt instructions,
 * bug table, eh_frame, etc.  DISCARDS must be the last of output
 * section definitions so that such archs put those in earlier section
 * definitions.
 */
#define DISCARDS							\
	/DISCARD/ : {							\
	EXIT_TEXT							\
	EXIT_DATA							\
	EXIT_CALL							\
	*(.discard)							\
	}

/**
 * PERCPU_VADDR - define output section for percpu area
 * @vaddr: explicit base address (optional)
 * @phdr: destination PHDR (optional)
 *
 * Macro which expands to output section for percpu area.  If @vaddr
 * is not blank, it specifies explicit base address and all percpu
 * symbols will be offset from the given address.  If blank, @vaddr
 * always equals @laddr + LOAD_OFFSET.
 *
 * @phdr defines the output PHDR to use if not blank.  Be warned that
 * output PHDR is sticky.  If @phdr is specified, the next output
 * section in the linker script will go there too.  @phdr should have
 * a leading colon.
 *
 * Note that this macros defines __per_cpu_load as an absolute symbol.
 * If there is no need to put the percpu section at a predetermined
 * address, use PERCPU().
 */
#define PERCPU_VADDR(vaddr, phdr)					\
	VMLINUX_SYMBOL(__per_cpu_load) = .;				\
	.data..percpu vaddr : AT(VMLINUX_SYMBOL(__per_cpu_load)		\
				- LOAD_OFFSET) {			\
		VMLINUX_SYMBOL(__per_cpu_start) = .;			\
		*(.data..percpu..first)					\
		*(.data..percpu..page_aligned)				\
		*(.data..percpu)					\
		*(.data..percpu..shared_aligned)			\
		VMLINUX_SYMBOL(__per_cpu_end) = .;			\
	} phdr								\
	. = VMLINUX_SYMBOL(__per_cpu_load) + SIZEOF(.data..percpu);

/**
 * PERCPU - define output section for percpu area, simple version
 * @align: required alignment
 *
 * Align to @align and outputs output section for percpu area.  This
 * macro doesn't maniuplate @vaddr or @phdr and __per_cpu_load and
 * __per_cpu_start will be identical.
 *
 * This macro is equivalent to ALIGN(align); PERCPU_VADDR( , ) except
 * that __per_cpu_load is defined as a relative symbol against
 * .data..percpu which is required for relocatable x86_32
 * configuration.
 */
#define PERCPU(align)							\
	. = ALIGN(align);						\
	.data..percpu	: AT(ADDR(.data..percpu) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__per_cpu_load) = .;			\
		VMLINUX_SYMBOL(__per_cpu_start) = .;			\
		*(.data..percpu..first)					\
		*(.data..percpu..page_aligned)				\
		*(.data..percpu)					\
		*(.data..percpu..shared_aligned)			\
		VMLINUX_SYMBOL(__per_cpu_end) = .;			\
	}


/*
 * Definition of the high level *_SECTION macros
 * They will fit only a subset of the architectures
 */


/*
 * Writeable data.
 * All sections are combined in a single .data section.
 * The sections following CONSTRUCTORS are arranged so their
 * typical alignment matches.
 * A cacheline is typical/always less than a PAGE_SIZE so
 * the sections that has this restriction (or similar)
 * is located before the ones requiring PAGE_SIZE alignment.
 * NOSAVE_DATA starts and ends with a PAGE_SIZE alignment which
 * matches the requirment of PAGE_ALIGNED_DATA.
 *
 * use 0 as page_align if page_aligned data is not used */
#define RW_DATA_SECTION(cacheline, pagealigned, inittask)		\
	. = ALIGN(PAGE_SIZE);						\
	.data : AT(ADDR(.data) - LOAD_OFFSET) {				\
		INIT_TASK_DATA(inittask)				\
		NOSAVE_DATA						\
		PAGE_ALIGNED_DATA(pagealigned)				\
		CACHELINE_ALIGNED_DATA(cacheline)			\
		READ_MOSTLY_DATA(cacheline)				\
		DATA_DATA						\
		CONSTRUCTORS						\
	}

#define INIT_TEXT_SECTION(inittext_align)				\
	. = ALIGN(inittext_align);					\
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(_sinittext) = .;				\
		INIT_TEXT						\
		VMLINUX_SYMBOL(_einittext) = .;				\
	}

#define INIT_DATA_SECTION(initsetup_align)				\
	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {		\
		INIT_DATA						\
		INIT_SETUP(initsetup_align)				\
		INIT_CALLS						\
		CON_INITCALL						\
		SECURITY_INITCALL					\
		INIT_RAM_FS						\
	}

#define BSS_SECTION(sbss_align, bss_align, stop_align)			\
	. = ALIGN(sbss_align);						\
	VMLINUX_SYMBOL(__bss_start) = .;				\
	SBSS(sbss_align)						\
	BSS(bss_align)							\
	. = ALIGN(stop_align);						\
	VMLINUX_SYMBOL(__bss_stop) = .;
