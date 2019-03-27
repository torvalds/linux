# $FreeBSD$
#
# awk script to convert a bdf file to C declarations in a form specialized
# for the mouse cursors in syscons/scvgarndr.c.  Usage:
#     awk -f thisfile < file.bdf < file.c
# The accompanying syscons mouse cursor bdf file has specialized comments
# which this script converts to details in the C declarations.
# This is not a general conversion utility, but produces reasonable output
# if the input is for a monospaced font of size between 9x16 and 16x16.

/^COMMENT cn.*mouse/ {
	gsub("[(),]", "")
	i = index($3, "-")
	n = substr($3, 1, i - 1)
	name[n] = $4
	i = index($4, "e")
	j = index($4, "x")
	k = index($4, "_")
	width[n] = substr($4, i + 1, j - i - 1)
	height[n] = substr($4, j + 1, k - j - 1)
	baspect[n] = $6
	iaspect[n] = $8
}
state == 0 && /^STARTCHAR/ {
	n = substr($2, 5)
	printf("static const struct mousedata %s = { {\n\t", name[n])
	state = 1
}
state >= 1 && state < 7 || state >= 7 + 16 && state < 7 + 16 + 7 {
	state++
	next
}
state >= 7 && state < 7 + 16 || state >= 7 + 16 + 7 && state < 7 + 16 + 7 +16 {
	printf("0x%s,", $1)
	if (state == 7 + 7 || state == 7 + 16 + 7 + 7)
		printf("\n\t")
	else if (state == 7 + 15)
		printf(" }, {\n\t")
	else if (state == 7 + 16 + 7 + 15) {
		printf(" },\n\t%s, %s, %s, %s, \"%s\",",
		    width[n], height[n], baspect[n], iaspect[n], name[n])
		printf("\n};\n\n")
		state = -1
	} else
		printf(" ")
	state++
	next
}
