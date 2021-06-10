/*
 * Common module linker script, always used when linking a module.
 * Archs are free to supply their own linker scripts.  ld will
 * combine them automatically.
 */
#include <asm/page.h>

SECTIONS {
	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
		/*
		 * LLD may emit .eh_frame with CONFIG_CFI_CLANG despite
		 * -fno-asynchronous-unwind-tables. Discard the section.
		 */
		*(.eh_frame)
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

#ifdef CONFIG_CFI_CLANG
	/*
	 * With CFI_CLANG, ensure __cfi_check is at the beginning of the
	 * .text section, and that the section is aligned to page size.
	 */
	.text : ALIGN(PAGE_SIZE) {
		*(.text.__cfi_check)
		*(.text .text.[0-9a-zA-Z_]*)
		__cfi_jt_start = .;
		*(.text..L.cfi.jumptable .text..L.cfi.jumptable.*)
		__cfi_jt_end = .;
	}
#endif
}

/* bring in arch-specific sections */
#include <asm/module.lds.h>
