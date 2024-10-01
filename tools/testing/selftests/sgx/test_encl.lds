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
		FILL(0xDEADBEEF);
		. = ALIGN(4096);
	} : text

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

ASSERT(!DEFINED(_GLOBAL_OFFSET_TABLE_), "Libcalls through GOT are not supported in enclaves")
