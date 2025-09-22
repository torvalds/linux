# $OpenBSD: extest.awk,v 1.3 2019/09/11 12:30:34 kettenis Exp $
# $NetBSD: extest.awk,v 1.6 2002/02/21 03:59:25 mrg Exp $

BEGIN {
	first = 1;

	printf("#include <sys/types.h>\n")
	printf("#include <sys/extent.h>\n\n")
	printf("#include <stdio.h>\n")
	printf("#include <stdlib.h>\n")
	printf("#include <string.h>\n")
	printf("int main(void) {\n")
	printf("struct extent *ex; int error; long result;\n")
}

END {
	printf("exit (0);\n")
	printf("}\n")
}

$1 == "extent" {
	if (first == 0) {
		printf("extent_destroy(ex);\n")
	}

	align = "EX_NOALIGN";
	boundary = "EX_NOBOUNDARY";

	printf("printf(\"output for %s\\n\");\n", $2)

	if ($5 == "") {
		flags = "0";
	} else {
		flags = $5;
	}
	printf("ex = extent_create(\"%s\", %s, %s, 0, 0, 0, %s);\n",
	       $2, $3, $4, flags)

	first = 0;
}

$1 == "align" {
	align = $2;
}

$1 == "boundary" {
	boundary = $2;
}

$1 == "alloc_region" {
	if ($4 == "") {
		flags = "0";
	} else {
		flags = $4;
	}
	printf("error = extent_alloc_region(ex, %s, %s, %s);\n",
	       $2, $3, flags)
	printf("if (error)\n\tprintf(\"error: %%s\\n\", strerror(error));\n")
}

$1 == "alloc_subregion" {
	printf("error = extent_alloc_subregion(ex, %s, %s, %s,\n",
	       $2, $3, $4)
	printf("\t%s, 0, %s, 0, &result);\n", align, boundary)
	printf("if (error)\n\tprintf(\"error: %%s\\n\", strerror(error));\n")
	printf("else\n\tprintf(\"result: 0x%%lx\\n\", result);\n")
}

$1 == "free" {
	if ($4 == "") {
		flags = "0";
	} else {
		flags = $4;
	}
	printf("error = extent_free(ex, %s, %s, %s);\n", $2, $3, flags)
	printf("if (error)\n\tprintf(\"error: %%s\\n\", strerror(error));\n")
}

$1 == "print" {
	printf("extent_print(ex);\n")
}
