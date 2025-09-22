#!/bin/sh -

random_uniform() {
	local	_upper_bound

	if [[ $1 -gt 0 ]]; then
		_upper_bound=$(($1 - 1))
	else
		_upper_bound=0
	fi

	echo `jot -r 1 0 $_upper_bound 2>/dev/null`
}

umask 007

PAGE_SIZE=`sysctl -n hw.pagesize`
PAD=$1
GAPDUMMY=$2

RANDOM1=`random_uniform $((3 * PAGE_SIZE))`
RANDOM2=`random_uniform $PAGE_SIZE`
RANDOM3=`random_uniform $PAGE_SIZE`
RANDOM4=`random_uniform $PAGE_SIZE`
RANDOM5=`random_uniform $PAGE_SIZE`

cat > gap.link << __EOF__

PHDRS {
	text PT_LOAD FILEHDR PHDRS;
	rodata PT_LOAD;
	data PT_LOAD;
	bss PT_LOAD;
}

SECTIONS {
	.text : ALIGN($PAGE_SIZE) {
		LONG($PAD);
		. += $RANDOM1;
		. = ALIGN($PAGE_SIZE);
		endboot = .;
		PROVIDE (endboot = .);
		. = ALIGN($PAGE_SIZE);
		. += $RANDOM2;
		. = ALIGN(16);
		*(.text .text.*)
	} :text =$PAD

	.rodata : {
		LONG($PAD);
		. += $RANDOM3;
		. = ALIGN(16);
		*(.rodata .rodata.*)
	} :rodata =$PAD

	.data : {
		LONG($PAD);
		. = . + $RANDOM4;	/* fragment of page */
		. = ALIGN(16);
		*(.data .data.*)
	} :data =$PAD

	.bss : {
		. = . + $RANDOM5;	/* fragment of page */
		. = ALIGN(16);
		*(.bss .bss.*)
	} :bss
}
__EOF__

$LD $LDFLAGS -r gap.link $GAPDUMMY -o gap.o
