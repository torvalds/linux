/*
 * FLAVORS
 */
struct core_reloc_flavors {
	int a;
	int b;
	int c;
};

/* this is not a flavor, as it doesn't have triple underscore */
struct core_reloc_flavors__err_wrong_name {
	int a;
	int b;
	int c;
};
