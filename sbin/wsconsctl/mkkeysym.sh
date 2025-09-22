#! /bin/sh
#
#	$OpenBSD: mkkeysym.sh,v 1.2 2010/06/28 20:40:39 maja Exp $
#	$NetBSD: mkkeysym.sh 1.1 1998/12/28 14:01:17 hannken Exp $
#
#	Build a table of keysyms from a file describing keysyms as:
#
#	/*BEGINKEYSYMDECL*/
#	#define KS_name 0xval
#	...
#	/*ENDKEYSYMDECL*/
#

AWK=${AWK:-awk}

${AWK} '
BEGIN {
	in_decl = 0;
	printf("/* DO  NOT EDIT: AUTOMATICALLY GENERATED FROM '$1' */\n\n");
	printf("#define\tKEYSYM_ENC_ISO\t0\n");
	printf("#define\tKEYSYM_ENC_L2\t1\n");
	printf("#define\tKEYSYM_ENC_L5\t2\n");
	printf("#define\tKEYSYM_ENC_L7\t3\n");
	printf("#define\tKEYSYM_ENC_KOI\t4\n\n");
	printf("struct ksym {\n\tchar *name;\n\tint value;\n\tint enc;\n};\n\n");
	printf("struct ksym ksym_tab_by_name[] = {\n");
}

END {
	printf("};\n");
}

$1 == "/*BEGINKEYSYMDECL*/" {
	in_decl = 1;
}

$1 == "/*ENDKEYSYMDECL*/" {
	in_decl = 0;
}

$1 ~ /^#[ 	]*define/ && $2 ~ /^KS_/ && $3 ~ /^0x[0-9a-f]*/ {
	if (in_decl) {
		enc="KEYSYM_ENC_ISO"
		if ($2 ~ /^KS_L2_/) { enc="KEYSYM_ENC_L2" }
		if ($2 ~ /^KS_L5_/) { enc="KEYSYM_ENC_L5" }
		if ($2 ~ /^KS_L7_/) { enc="KEYSYM_ENC_L7" }
		if ($2 ~ /^KS_Cyrillic_/) { enc="KEYSYM_ENC_KOI" }
		printf("\t{ \"%s\", %s, %s },\n", substr($2, 4), $3, enc);
	}
}' $1
