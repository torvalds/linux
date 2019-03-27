# $FreeBSD$

BEGIN {
	print "# Warning: Do not edit. This is automatically extracted"
	print "# from CLDR project data, obtained from http://cldr.unicode.org/"
	print "# -----------------------------------------------------------------------------"
}
$1 == "comment_char" { print }
$1 == "escape_char" { print }
$1 == "LC_COLLATE" { doprint = 1 }
doprint == 1 { print }
$1 == "END" && $2 == "LC_COLLATE" { exit 0 }
