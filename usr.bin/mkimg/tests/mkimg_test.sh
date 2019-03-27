# $FreeBSD$

mkimg_blksz_list="512 4096"
mkimg_format_list="qcow qcow2 raw vhd vhdf vmdk"
mkimg_geom_list="1x1 63x255"
mkimg_scheme_list="apm bsd ebr gpt mbr vtoc8"

bootcode()
{
    case $1 in
      bsd)	echo 8192 ;;
      gpt|mbr)	echo 512 ;;
      *)	echo 0 ;;
    esac
    return 0
}

mkcontents()
{
    local byte count name

    byte=$1
    count=$2

    name=_tmp-$byte-$count.bin
    jot -b $byte $(($count/2)) > $name
    echo $name
    return 0
}

makeimage()
{
    local blksz bootarg bootsz format geom nhds nsecs partarg pfx scheme

    format=$1
    scheme=$2
    blksz=$3
    geom=$4
    pfx=$5
    shift 5

    nsecs=${geom%x*}
    nhds=${geom#*x}

    bootsz=`bootcode $scheme`
    if test $bootsz -gt 0; then
	bootarg="-b `mkcontents B $bootsz`"
    else
	bootarg=""
    fi

    partarg=""
    for P in $*; do
	partarg="$partarg -p $P"
    done
    if test -z "$partarg"; then
	local swap ufs
	swap="-p freebsd-swap::128K"
	ufs="-p freebsd-ufs:=`mkcontents P 4194304`"
	partarg="$ufs $swap"
    fi

    imagename=$pfx-$geom-$blksz-$scheme.$format

    mkimg -y -f $format -o $imagename -s $scheme -P $blksz -H $nhds -T $nsecs \
	    $bootarg $partarg
    echo $imagename
    return 0
}

mkimg_rebase()
{
    local baseline image result tmpfile update

    image=$1
    result=$2

    baseline=$image.hex
    update=yes

    if test -f $baseline; then
	tmpfile=_tmp-baseline
	sed -e '/^#.*/D' < $baseline > $tmpfile
	if diff -u $tmpfile $result; then
	    update=no
	fi
    fi

    if test $update = yes; then
	# Prevent keyword expansion when writing the keyword.
	(echo -n '# $'; echo -n FreeBSD; echo '$') > $baseline
	cat $result >> $baseline
    fi

    rm $image $result _tmp-*
    return 0
}

mkimg_test()
{
    local blksz format geom scheme

    geom=$1
    blksz=$2
    scheme=$3
    format=$4

    case $scheme in
      ebr|mbr)
	bsd=`makeimage raw bsd $blksz $geom _tmp`
	partinfo="freebsd:=$bsd"
	;;
      *)
	partinfo=""
	;;
    esac
    image=`makeimage $format $scheme $blksz $geom img $partinfo`
    result=$image.out
    hexdump -C $image > $result
    if test "x$mkimg_update_baseline" = "xyes"; then
	mkimg_rebase $image $result
    else
	baseline=`atf_get_srcdir`/$image
	atf_check -s exit:0 diff -u $baseline $result
    fi
    return 0
}

atf_test_case rebase
rebase_body()
{
    local nm

    mkimg_update_baseline=yes
    for nm in $mkimg_tests; do
	${nm}_body
    done
    return 0
}

atf_init_test_cases()
{
    local B F G S nm

    for G in $mkimg_geom_list; do
	for B in $mkimg_blksz_list; do
	    for S in $mkimg_scheme_list; do
		for F in $mkimg_format_list; do
		    nm="${S}_${G}_${B}_${F}"
		    atf_test_case $nm
		    eval "${nm}_body() { mkimg_test $G $B $S $F; }"
		    mkimg_tests="${mkimg_tests} ${nm}"
		    atf_add_test_case $nm
		done
	    done
	done
    done

    # XXX hack to make updating the baseline easier
    if test "${__RUNNING_INSIDE_ATF_RUN}" != "internal-yes-value"; then
	atf_add_test_case rebase
    fi
}

