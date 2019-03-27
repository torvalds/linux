#
# Extended Regular Expression

# skip comments
$0 ~ /^#/ { next; }

# skip specifics to regcomp/regexec
$2 ~ /[msnr$#p^]/ { next; }

# jump empty lines
$0 ~ /^$/ { next; }

# subreg skip
NF >= 5 { next; }

# debug
#{ printf ("<%s> <%s> <%s> <%s>\n", $1, $2, $3, $4); }

# errors
NF == 3 {
# nuke any remaining '@'
#	gsub (/@/, ",");
# it means empty lines
	gsub (/\"\"/, "");
# escapes
	gsub (/\\\'/, "\\\'\'");
# error in regex 
	if (index ($2, "C") != 0)
	{
		if (index ($2, "b") == 0)
			printf ("2@%s@%s\n", $1, $3);
	}
# error not matching 
	else
	{
		if (index ($2, "b") == 0)
			printf ("1@%s@%s\n", $1, $3);
	}
	next;
}

# ok
NF == 4 {
# skip those magic cookies can't rely on echo to gnerate them
	if (match($3, /[NSTZ]/))
		next;

# nuke any remaining '@'
#	gsub (/@/, ",");
# it means empty lines
	gsub (/\"\"/, "");
# escape escapes
	gsub (/\\\'/, "\\\'\'");

	if (index ($2, "b") == 0)
	{
		printf ("0@%s@%s\n", $1, $3);
	}
	next;
}
