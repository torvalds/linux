OUTPUT_FORMAT(elf64-x86-64)

PHDRS
{
	tcs PT_LOAD;
	text PT_LOAD;
	data PT_LOAD;
}

SECTIONS
{
	. = 0;
	.tcs : {
		*(.tcs*)
	} : tcs

	. = ALIGN(4096);
	.text : {
		*(.text*)
		*(.rodata*)
	} : text

	. = ALIGN(4096);
	.data : {
		*(.data*)
	} : data

	/DISCARD/ : {
		*(.comment*)
		*(.note*)
		*(.debug*)
		*(.eh_frame*)
	}
}

ASSERT(!DEFINED(.altinstructions), "ALTERNATIVES are not supported in enclaves")
ASSERT(!DEFINED(.altinstr_replacement), "ALTERNATIVES are not supported in enclaves")
ASSERT(!DEFINED(.discard.retpoline_safe), "RETPOLINE ALTERNATIVES are not supported in enclaves")
ASSERT(!DEFINED(.discard.nospec), "RETPOLINE ALTERNATIVES are not supported in enclaves")
ASSERT(!DEFINED(.got.plt), "Libcalls are not supported in enclaves")
