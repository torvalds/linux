#ifndef LOAD_OFFSET
#define LOAD_OFFSET 0
#endif

#ifndef VMLINUX_SYMBOL
#define VMLINUX_SYMBOL(_sym_) _sym_
#endif

/* Align . to a 8 byte boundary equals to maximum function alignment. */
#define ALIGN_FUNCTION()  . = ALIGN(8)

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


/* .data section */
#define DATA_DATA							\
	*(.data)							\
	*(.data.init.refok)						\
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
	VMLINUX_SYMBOL(__stop___markers) = .;

#define RO_DATA(align)							\
	. = ALIGN((align));						\
	.rodata           : AT(ADDR(.rodata) - LOAD_OFFSET) {		\
		VMLINUX_SYMBOL(__start_rodata) = .;			\
		*(.rodata) *(.rodata.*)					\
		*(__vermagic)		/* Kernel version magic */	\
		*(__markers_strings)	/* Markers: strings */		\
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
	.rio_route        : AT(ADDR(.rio_route) - LOAD_OFFSET) {	\
		VMLINUX_SYMBOL(__start_rio_route_ops) = .;		\
		*(.rio_route_ops)					\
		VMLINUX_SYMBOL(__end_rio_route_ops) = .;		\
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

/* RODATA provided for backward compatibility.
 * All archs are supposed to use RO_DATA() */
#define RODATA RO_DATA(4096)

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
		*(.text.init.refok)					\
		*(.exit.text.refok)					\
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

/* Section used for early init (in .S files) */
#define HEAD_TEXT  *(.head.text)

/* init and exit section handling */
#define INIT_DATA							\
	*(.init.data)							\
	DEV_DISCARD(init.data)						\
	DEV_DISCARD(init.rodata)					\
	CPU_DISCARD(init.data)						\
	CPU_DISCARD(init.rodata)					\
	MEM_DISCARD(init.data)						\
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

		/* DWARF debug sections.
		Symbols in the DWARF debugging sections are relative to
		the beginning of the section so we begin them at 0.  */
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

#define PERCPU(align)							\
	. = ALIGN(align);						\
	VMLINUX_SYMBOL(__per_cpu_start) = .;				\
	.data.percpu  : AT(ADDR(.data.percpu) - LOAD_OFFSET) {		\
		*(.data.percpu.page_aligned)				\
		*(.data.percpu)						\
		*(.data.percpu.shared_aligned)				\
	}								\
	VMLINUX_SYMBOL(__per_cpu_end) = .;
