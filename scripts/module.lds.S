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

SECTIONS {
	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
	}

	__ksymtab		0 : { *(SORT(___ksymtab+*)) }
	__ksymtab_gpl		0 : { *(SORT(___ksymtab_gpl+*)) }
	__kcrctab		0 : { *(SORT(___kcrctab+*)) }
	__kcrctab_gpl		0 : { *(SORT(___kcrctab_gpl+*)) }

	.ctors			0 : ALIGN(8) { *(SORT(.ctors.*)) *(.ctors) }
	.init_array		0 : ALIGN(8) { *(SORT(.init_array.*)) *(.init_array) }

	.altinstructions	0 : ALIGN(8) { KEEP(*(.altinstructions)) }
	__bug_table		0 : ALIGN(8) { KEEP(*(__bug_table)) }
	__jump_table		0 : ALIGN(8) { KEEP(*(__jump_table)) }

	__patchable_function_entries : { *(__patchable_function_entries) }

#ifdef CONFIG_ARCH_USES_CFI_TRAPS
	__kcfi_traps 		: { KEEP(*(.kcfi_traps)) }
#endif

#if IS_ENABLED(CONFIG_CRYPTO_FIPS140_MOD)
	/*
	 * The FIPS140 module incorporates copies of builtin code, which gets
	 * integrity checked at module load time, and registered in a way that
	 * ensures that the integrity checked versions supersede the builtin
	 * ones.  These objects are compiled as builtin code, and so their init
	 * hooks will be exported from the binary in the same way as builtin
	 * initcalls are, i.e., annotated with a level that defines the order
	 * in which the hooks are expected to be invoked.
	 */
#define INIT_CALLS_LEVEL(level)						\
		KEEP(*(.initcall##level##.init*))			\
		KEEP(*(.initcall##level##s.init*))

	.initcalls : {
		*(.initcalls._start)
		INIT_CALLS_LEVEL(0)
		INIT_CALLS_LEVEL(1)
		INIT_CALLS_LEVEL(2)
		INIT_CALLS_LEVEL(3)
		INIT_CALLS_LEVEL(4)
		INIT_CALLS_LEVEL(5)
		INIT_CALLS_LEVEL(rootfs)
		INIT_CALLS_LEVEL(6)
		INIT_CALLS_LEVEL(7)
		*(.initcalls._end)
	}
#endif

#if defined(CONFIG_LTO_CLANG) || IS_ENABLED(CONFIG_CRYPTO_FIPS140_MOD)
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
		*(.rodata.._start)
		*(.rodata .rodata.[0-9a-zA-Z_]*)
		*(.rodata..L*)
		*(.rodata.._end)
	}

	.text : {
		*(.text.._start)
		*(.text .text.[0-9a-zA-Z_]*)
		*(.text.._end)
		*(.text.._fips140_unchecked)
	}
#endif
}

/* bring in arch-specific sections */
#include <asm/module.lds.h>
