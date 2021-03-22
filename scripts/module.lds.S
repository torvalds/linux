/*
 * Common module linker script, always used when linking a module.
 * Archs are free to supply their own linker scripts.  ld will
 * combine them automatically.
 */
SECTIONS {
	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
	}

	__ksymtab		0 : { *(SORT(___ksymtab+*)) }
	__ksymtab_gpl		0 : { *(SORT(___ksymtab_gpl+*)) }
	__ksymtab_unused	0 : { *(SORT(___ksymtab_unused+*)) }
	__ksymtab_unused_gpl	0 : { *(SORT(___ksymtab_unused_gpl+*)) }
	__ksymtab_gpl_future	0 : { *(SORT(___ksymtab_gpl_future+*)) }
	__kcrctab		0 : { *(SORT(___kcrctab+*)) }
	__kcrctab_gpl		0 : { *(SORT(___kcrctab_gpl+*)) }
	__kcrctab_unused	0 : { *(SORT(___kcrctab_unused+*)) }
	__kcrctab_unused_gpl	0 : { *(SORT(___kcrctab_unused_gpl+*)) }
	__kcrctab_gpl_future	0 : { *(SORT(___kcrctab_gpl_future+*)) }

	.init_array		0 : ALIGN(8) { *(SORT(.init_array.*)) *(.init_array) }

	__jump_table		0 : ALIGN(8) { KEEP(*(__jump_table)) }

	__patchable_function_entries : { *(__patchable_function_entries) }

#ifdef CONFIG_LTO_CLANG
	/*
	 * With CONFIG_LTO_CLANG, LLD always enables -fdata-sections and
	 * -ffunction-sections, which increases the size of the final module.
	 * Merge the split sections in the final binary.
	 */
	.bss : {
		*(.bss .bss.[0-9a-zA-Z_]*)
		*(.bss..L*)
	}

	.data : {
		*(.data .data.[0-9a-zA-Z_]*)
		*(.data..L*)
	}

	.rodata : {
		*(.rodata .rodata.[0-9a-zA-Z_]*)
		*(.rodata..L*)
	}

	.text : { *(.text .text.[0-9a-zA-Z_]*) }
#endif
}

/* bring in arch-specific sections */
#include <asm/module.lds.h>
