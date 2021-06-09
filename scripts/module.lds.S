/*
 * Common module linker script, always used when linking a module.
 * Archs are free to supply their own linker scripts.  ld will
 * combine them automatically.
 */
#ifdef CONFIG_CFI_CLANG
# include <asm/page.h>
# define ALIGN_CFI 		ALIGN(PAGE_SIZE)
# define SANITIZER_DISCARDS	*(.eh_frame)
#else
# define ALIGN_CFI
# define SANITIZER_DISCARDS
#endif

SECTIONS {
	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
		SANITIZER_DISCARDS
	}

	__ksymtab		0 : { *(SORT(___ksymtab+*)) }
	__ksymtab_gpl		0 : { *(SORT(___ksymtab_gpl+*)) }
	__kcrctab		0 : { *(SORT(___kcrctab+*)) }
	__kcrctab_gpl		0 : { *(SORT(___kcrctab_gpl+*)) }

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

	/*
	 * With CONFIG_CFI_CLANG, we assume __cfi_check is at the beginning
	 * of the .text section, and is aligned to PAGE_SIZE.
	 */
	.text : ALIGN_CFI {
		*(.text.__cfi_check)
		*(.text .text.[0-9a-zA-Z_]* .text..L.cfi*)
	}
#endif
}

/* bring in arch-specific sections */
#include <asm/module.lds.h>
