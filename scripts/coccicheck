#!/bin/bash

SPATCH="`which ${SPATCH:=spatch}`"

# The verbosity may be set by the environmental parameter V=
# as for example with 'make V=1 coccicheck'

if [ -n "$V" -a "$V" != "0" ]; then
	VERBOSE=1
else
	VERBOSE=0
fi

FLAGS="$SPFLAGS -very_quiet"

# spatch only allows include directories with the syntax "-I include"
# while gcc also allows "-Iinclude" and "-include include"
COCCIINCLUDE=${LINUXINCLUDE//-I/-I }
COCCIINCLUDE=${COCCIINCLUDE//-include/-I}

if [ "$C" = "1" -o "$C" = "2" ]; then
    ONLINE=1

    # Take only the last argument, which is the C file to test
    shift $(( $# - 1 ))
    OPTIONS="$COCCIINCLUDE $1"
else
    ONLINE=0
    if [ "$KBUILD_EXTMOD" = "" ] ; then
        OPTIONS="-dir $srctree $COCCIINCLUDE"
    else
        OPTIONS="-dir $KBUILD_EXTMOD $COCCIINCLUDE"
    fi
fi

if [ "$KBUILD_EXTMOD" != "" ] ; then
    OPTIONS="-patch $srctree $OPTIONS"
fi

if [ ! -x "$SPATCH" ]; then
    echo 'spatch is part of the Coccinelle project and is available at http://coccinelle.lip6.fr/'
    exit 1
fi

if [ "$MODE" = "" ] ; then
    if [ "$ONLINE" = "0" ] ; then
	echo 'You have not explicitly specified the mode to use. Using default "chain" mode.'
	echo 'All available modes will be tried (in that order): patch, report, context, org'
	echo 'You can specify the mode with "make coccicheck MODE=<mode>"'
    fi
    MODE="chain"
elif [ "$MODE" = "report" -o "$MODE" = "org" ] ; then
    FLAGS="$FLAGS -no_show_diff"
fi

if [ "$ONLINE" = "0" ] ; then
    echo ''
    echo 'Please check for false positives in the output before submitting a patch.'
    echo 'When using "patch" mode, carefully review the patch before submitting it.'
    echo ''
fi

run_cmd() {
	if [ $VERBOSE -ne 0 ] ; then
		echo "Running: $@"
	fi
	eval $@
}


coccinelle () {
    COCCI="$1"

    OPT=`grep "Option" $COCCI | cut -d':' -f2`

#   The option '-parse_cocci' can be used to syntactically check the SmPL files.
#
#    $SPATCH -D $MODE $FLAGS -parse_cocci $COCCI $OPT > /dev/null

    if [ $VERBOSE -ne 0 -a $ONLINE -eq 0 ] ; then

	FILE=`echo $COCCI | sed "s|$srctree/||"`

	echo "Processing `basename $COCCI`"
	echo "with option(s) \"$OPT\""
	echo ''
	echo 'Message example to submit a patch:'

	sed -ne 's|^///||p' $COCCI

	if [ "$MODE" = "patch" ] ; then
	    echo ' The semantic patch that makes this change is available'
	elif [ "$MODE" = "report" ] ; then
	    echo ' The semantic patch that makes this report is available'
	elif [ "$MODE" = "context" ] ; then
	    echo ' The semantic patch that spots this code is available'
	elif [ "$MODE" = "org" ] ; then
	    echo ' The semantic patch that makes this Org report is available'
	else
	    echo ' The semantic patch that makes this output is available'
	fi
	echo " in $FILE."
	echo ''
	echo ' More information about semantic patching is available at'
	echo ' http://coccinelle.lip6.fr/'
	echo ''

	if [ "`sed -ne 's|^//#||p' $COCCI`" ] ; then
	    echo 'Semantic patch information:'
	    sed -ne 's|^//#||p' $COCCI
	    echo ''
	fi
    fi

    if [ "$MODE" = "chain" ] ; then
	run_cmd $SPATCH -D patch   \
		$FLAGS -sp_file $COCCI $OPT $OPTIONS               || \
	run_cmd $SPATCH -D report  \
		$FLAGS -sp_file $COCCI $OPT $OPTIONS -no_show_diff || \
	run_cmd $SPATCH -D context \
		$FLAGS -sp_file $COCCI $OPT $OPTIONS               || \
	run_cmd $SPATCH -D org     \
		$FLAGS -sp_file $COCCI $OPT $OPTIONS -no_show_diff || exit 1
    elif [ "$MODE" = "rep+ctxt" ] ; then
	run_cmd $SPATCH -D report  \
		$FLAGS -sp_file $COCCI $OPT $OPTIONS -no_show_diff && \
	run_cmd $SPATCH -D context \
		$FLAGS -sp_file $COCCI $OPT $OPTIONS || exit 1
    else
	run_cmd $SPATCH -D $MODE   $FLAGS -sp_file $COCCI $OPT $OPTIONS || exit 1
    fi

}

if [ "$COCCI" = "" ] ; then
    for f in `find $srctree/scripts/coccinelle/ -name '*.cocci' -type f | sort`; do
	coccinelle $f
    done
else
    coccinelle $COCCI
fi
