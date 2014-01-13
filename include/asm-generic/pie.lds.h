/*
 * Helper macros to support writing architecture specific
 * pie linker scripts.
 *
 * A minimal linker scripts has following content:
 * [This is a sample, architectures may have special requiriements]
 *
 * OUTPUT_FORMAT(...)
 * OUTPUT_ARCH(...)
 * SECTIONS
 * {
 *	. = 0x0;
 *
 *	PIE_COMMON_START
 *	.text {
 *		PIE_TEXT_TEXT
 *	}
 *	PIE_COMMON_END
 *
 *	PIE_OVERLAY_START
 *	OVERLAY : NOCROSSREFS {
 *		PIE_OVERLAY_SECTION(am33xx)
 *		PIE_OVERLAY_SECTION(am347x)
 *		[...]
 *	}
 *	PIE_OVERLAY_END
 *
 *	PIE_DISCARDS		// must be the last
 * }
 */

#include <asm-generic/vmlinux.lds.h>

#define PIE_COMMON_START						\
	__pie_common_start : {						\
		VMLINUX_SYMBOL(__pie_common_start) = .;			\
	}

#define PIE_COMMON_END							\
	__pie_common_end : {						\
		VMLINUX_SYMBOL(__pie_common_end) = .;			\
	}

#define PIE_OVERLAY_START						\
	__pie_overlay_start : {						\
		VMLINUX_SYMBOL(__pie_overlay_start) = .;		\
	}

#define PIE_OVERLAY_END							\
	__pie_overlay_end : {						\
		VMLINUX_SYMBOL(__pie_overlay_end) = .;			\
	}

#define PIE_TEXT_TEXT							\
	KEEP(*(.pie.text))

#define PIE_OVERLAY_SECTION(name)					\
	.pie.##name {							\
		KEEP(*(.pie.##name##.*))				\
		VMLINUX_SYMBOL(__pie_##name##_start) =			\
				LOADADDR(.pie.##name##);		\
		VMLINUX_SYMBOL(__pie_##name##_end) =			\
				LOADADDR(.pie.##name##) +		\
				SIZEOF(.pie.##name##);			\
	}								\
	.rel.##name {							\
		KEEP(*(.rel.pie.##name##.*))				\
		VMLINUX_SYMBOL(__pie_rel_##name##_start) =		\
				LOADADDR(.rel.##name##);		\
		VMLINUX_SYMBOL(__pie_rel_##name##_end) =		\
				LOADADDR(.rel.##name##) +		\
				SIZEOF(.rel.##name##);			\
	}

#define PIE_DISCARDS							\
	/DISCARD/ : {							\
	*(.dynsym)							\
	*(.dynstr*)							\
	*(.dynamic*)							\
	*(.plt*)							\
	*(.interp*)							\
	*(.gnu*) 							\
	*(.hash)							\
	*(.comment)							\
	*(.bss*)							\
	*(.data)							\
	*(.discard)							\
	*(.discard.*)							\
	}

