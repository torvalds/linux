# first line of input is the variable declaration, don't touch that
2,$ {  
# XXX beware of the order ! we have to quote \ and " before inserting \n"
	s/\\/\\\\/g
	s/"/\\"/g
	s/^/    "/
	s/$/\\n"/
}
# and append a ; at the end !
$s/$/;/
