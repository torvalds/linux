# This script generates the PS3 hypervisor call stubs from an HV
# interface definition file. The PS3 HV calling convention is very
# similar to the PAPR one, except that the function token is passed in
# r11 instead of r3.
#
# Invoke like so: awk -f ps3-hv-asm.awk < ps3-hvcall.master > ps3-hvcall.S
#

# $FreeBSD$

BEGIN {
	printf("/* $FreeBSD$ */\n\n");
	printf("#include <machine/asm.h>\n\n");
	printf("#define hc .long 0x44000022\n\n");
}

/HVCALL.*/ {
	code = $2;
	ins = split($4, a, ",")
	outs = split($5, a, ",")
	
	printf("ASENTRY(%s)\n",$3);
	printf("\tmflr	%%r0\n");
	printf("\tstd	%%r0,16(%%r1)\n");
	printf("\tstdu	%%r1,-%d(%%r1)\n", 48+8*outs);

	if ($4 == "UNUSED")
		ins = 0
	
	# Save output reg addresses to the stack
	for (i = 0; i < outs; i++) {
		if (ins+i >= 8) {
		   printf("\tld	%%r11,%d(%%r1)\n", 48+8*outs + 48 + 8*(i+ins));
		   printf("\tstd	%%r11,%d(%%r1)\n", 48+8*i);
		} else {
		   printf("\tstd	%%r%d,%d(%%r1)\n", 3+ins+i, 48+8*i);
		}
	}

	printf("\tli	%%r11,%d\n", code);
	printf("\thc\n");
	printf("\textsw	%%r3,%%r3\n");
		
	for (i = 0; i < outs; i++) {
		printf("\tld	%%r11,%d(%%r1)\n", 48+8*i);
		printf("\tstd	%%r%d,0(%%r11)\n", 4+i);
	}

	printf("\tld	%%r1,0(%%r1)\n");
	printf("\tld	%%r0,16(%%r1)\n");
	printf("\tmtlr	%%r0\n");
	printf("\tblr\n\n");
}
