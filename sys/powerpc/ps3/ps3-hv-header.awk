# This script generates the PS3 hypervisor call header from a hypervisor
# interface definition file. All lines that do not begin with HVCALL
# or a bare # for comments are copied to the output header so that
# enums, constant, C comments and the like can be passed through into the
# header.
#
# Invoke like so: awk -f ps3-hv-header.awk < ps3-hvcall.master > ps3-hv.h
#

# $FreeBSD$

!/HVCALL.*/ && (!/#.*/ || /#define.*/ || /#include.*/) {
	print($0);
}

/HVCALL.*/ {
	split($5, outs, ",")
	if ($4 == "UNUSED")
		split("", ins, ",")
	else
		split($4, ins, ",")

	printf("int %s(",$3);
	for (i = 1; i <= length(ins); i++) {
		printf("uint64_t %s", ins[i]);
		if (i < length(ins)) printf(", ");
	}

	if (length(outs) > 0 && length(ins) > 0)
		printf(", ");

	for (i = 1; i <= length(outs); i++) {
		printf("uint64_t *%s", outs[i]);
		if (i < length(outs)) printf(", ");
	}

	if (length(outs) == 0 && length(ins) == 0)
		printf("void");

	printf(");\n");
}
	
