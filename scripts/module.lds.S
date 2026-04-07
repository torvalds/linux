/*
 * Common module linker script, always used when linking a module.
 * Archs are free to supply their own linker scripts.  ld will
 * combine them automatically.
 */
#ifdef CONFIG_UNWIND_TABLES
#define DISCARD_EH_FRAME
#else
#define DISCARD_EH_FRAME	*(.eh_frame)
#endif

#include <asm-generic/codetag.lds.h>

SECTIONS {
	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
		*(.export_symbol)
		*(.no_trim_symbol)
	}

	__ksymtab		0 : ALIGN(8) { *(SORT(___ksymtab+*)) }
	__kcrctab		0 : ALIGN(4) { *(SORT(___kcrctab+*)) }
	__kflagstab		0 : ALIGN(1) { *(SORT(___kflagstab+*)) }

	.ctors			0 : ALIGN(8) { *(SORT(.ctors.*)) *(.ctors) }
	.init_array		0 : ALIGN(8) { *(SORT(.init_array.*)) *(.init_array) }

	.altinstructions	0 : ALIGN(8) { KEEP(*(.altinstructions)) }
	__bug_table		0 : ALIGN(8) { KEEP(*(__bug_table)) }
	__jump_table		0 : ALIGN(8) { KEEP(*(__jump_table)) }
	__ex_table		0 : ALIGN(4) { KEEP(*(__ex_table)) }

	__patchable_function_entries 0 : { *(__patchable_function_entries) }

	.init.klp_funcs		0 : ALIGN(8) { KEEP(*(.init.klp_funcs)) }
	.init.klp_objects	0 : ALIGN(8) { KEEP(*(.init.klp_objects)) }

#ifdef CONFIG_ARCH_USES_CFI_TRAPS
	__kcfi_traps		0 : { KEEP(*(.kcfi_traps)) }
#endif

#ifndef CONFIG_ARCH_WANTS_MODULES_TEXT_SECTIONS
	.text			0 : {
		*(.text .text.[0-9a-zA-Z_]*)
	}
#endif

	.bss			0 : {
		*(.bss .bss.[0-9a-zA-Z_]*)
		*(.bss..L*)
	}

	.data			0 : {
		*(.data .data.[0-9a-zA-Z_]*)
		*(.data..L*)
	}

	.rodata			0 : {
		*(.rodata .rodata.[0-9a-zA-Z_]*)
		*(.rodata..L*)
	}

	MOD_SEPARATE_CODETAG_SECTIONS()
}

/* bring in arch-specific sections */
#include <asm/module.lds.h>
