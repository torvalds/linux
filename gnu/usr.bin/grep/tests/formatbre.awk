#
# Basic Regular Expression

# kip comments
$0 ~ /^#/ { next; }

# skip those option specific to regexec/regcomp
$2 ~ /[msnr$#p^]/ { next; }

# skip empty lines
$0 ~ /^$/ { next; }

# debug
#{ printf ("<%s> <%s> <%s> <%s>\n", $1, $2, $3, $4); }

# subreg expresion
NF >= 5 { next; }

# errors
NF == 3 {
#	gsub (/@/, ",");
# it means empty lines
    gsub (/\"\"/, "");
# escapes
    gsub (/\\\'/, "\\\'\'");
# error in regex
	if (index ($2, "C") != 0)
	{
		if (index ($2, "b") != 0)
			printf ("2@%s@%s\n", $1, $3);
	}
# erro no match
	else
	{
		if (index ($2, "b") != 0)
			printf ("1@%s@%s\n", $1, $3);
	}
	next;
}

# ok
NF == 4 {
# skip those magic cookies can't rely on echo to gnerate them
    if (match($3, /[NSTZ]/))
        next;

#	gsub (/@/, ",");
# it means empty lines
    gsub (/\"\"/, "");
# escape escapes
    gsub (/\\\'/, "\\\'\'");

	if (index ($2, "b") != 0)
		printf ("0@%s@%s\n", $1, $3);
}
