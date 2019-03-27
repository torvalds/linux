# $FreeBSD$

atf_test_case bmpat
bmpat_head()
{
	atf_set "descr" "Check matching multibyte characters (PR153502)"
}
bmpat_body()
{
	export LC_CTYPE="C.UTF-8"

	printf 'é' | atf_check -o "inline:é" \
	    sed -ne '/^.$/p'
	printf 'éé' | atf_check -o "inline:éé" \
	    sed -ne '/^..$/p'
	printf 'aéa' | atf_check -o "inline:aéa" \
	    sed -ne '/a.a/p'
	printf 'aéa'| atf_check -o "inline:aéa" \
	    sed -ne '/a.*a/p'
	printf 'aaéaa' | atf_check -o "inline:aaéaa" \
	    sed -ne '/aa.aa/p'
	printf 'aéaéa' | atf_check -o "inline:aéaéa" \
	    sed -ne '/a.a.a/p'
	printf 'éa' | atf_check -o "inline:éa" \
	    sed -ne '/.a/p'
	printf 'aéaa' | atf_check -o "inline:aéaa" \
	    sed -ne '/a.aa/p'
	printf 'éaé' | atf_check -o "inline:éaé" \
	    sed -ne '/.a./p'
}

atf_test_case icase
icase_head()
{
	atf_set "descr" "Check case-insensitive matching for characters 128-255"
}
icase_body()
{
	export LC_CTYPE="C.UTF-8"

	a=$(printf '\302\265\n')	# U+00B5
	b=$(printf '\316\234\n')	# U+039C
	c=$(printf '\316\274\n')	# U+03BC

	echo $b | atf_check -o "inline:$b\n" sed -ne "/$a/Ip"
	echo $c | atf_check -o "inline:$c\n" sed -ne "/$a/Ip"
}

atf_init_test_cases()
{
	atf_add_test_case bmpat
	atf_add_test_case icase
}
