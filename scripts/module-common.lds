/*
 * Common module linker script, always used when linking a module.
 * Archs are free to supply their own linker scripts.  ld will
 * combine them automatically.
 */
SECTIONS {
	/DISCARD/ : { *(.discard) }

	__ksymtab		: { *(SORT(___ksymtab+*)) }
	__ksymtab_gpl		: { *(SORT(___ksymtab_gpl+*)) }
	__ksymtab_unused	: { *(SORT(___ksymtab_unused+*)) }
	__ksymtab_unused_gpl	: { *(SORT(___ksymtab_unused_gpl+*)) }
	__ksymtab_gpl_future	: { *(SORT(___ksymtab_gpl_future+*)) }
	__kcrctab		: { *(SORT(___kcrctab+*)) }
	__kcrctab_gpl		: { *(SORT(___kcrctab_gpl+*)) }
	__kcrctab_unused	: { *(SORT(___kcrctab_unused+*)) }
	__kcrctab_unused_gpl	: { *(SORT(___kcrctab_unused_gpl+*)) }
	__kcrctab_gpl_future	: { *(SORT(___kcrctab_gpl_future+*)) }
}
