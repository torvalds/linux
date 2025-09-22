BEGIN {
	print("/* THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT. */");
	print("static const struct pci_matchid radeon_devices[] = {");
}
/0, CHIP/ {
	val = substr($0, 3, 14);
	printf("\t{ " val " },\n");
}
END {
	print("};");
}
