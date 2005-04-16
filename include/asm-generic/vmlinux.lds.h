#ifndef LOAD_OFFSET
#define LOAD_OFFSET 0
#endif

#ifndef VMLINUX_SYMBOL
#define VMLINUX_SYMBOL(_sym_) _sym_
#endif

#define RODATA								\
	.rodata           : AT(ADDR(.rodata) - LOAD_OFFSET) {		\
		*(.rodata) *(.rodata.*)					\
		*(__vermagic)		/* Kernel version magic */	\
	}								\
									\
	.rodata1          : AT(ADDR(.rodata1) - LOAD_OFFSET) {		\
		*(.rodata1)						\
	}								\
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
	}								\
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
	/* Kernel symbol table: strings */				\
        __ksymtab_strings : AT(ADDR(__ksymtab_strings) - LOAD_OFFSET) {	\
		*(__ksymtab_strings)					\
	}								\
									\
	/* Built-in module parameters. */				\
	__param : AT(ADDR(__param) - LOAD_OFFSET) {			\
		VMLINUX_SYMBOL(__start___param) = .;			\
		*(__param)						\
		VMLINUX_SYMBOL(__stop___param) = .;			\
	}

#define SECURITY_INIT							\
	.security_initcall.init : {					\
		VMLINUX_SYMBOL(__security_initcall_start) = .;		\
		*(.security_initcall.init) 				\
		VMLINUX_SYMBOL(__security_initcall_end) = .;		\
	}

#define SCHED_TEXT							\
		VMLINUX_SYMBOL(__sched_text_start) = .;			\
		*(.sched.text)						\
		VMLINUX_SYMBOL(__sched_text_end) = .;

#define LOCK_TEXT							\
		VMLINUX_SYMBOL(__lock_text_start) = .;			\
		*(.spinlock.text)					\
		VMLINUX_SYMBOL(__lock_text_end) = .;
