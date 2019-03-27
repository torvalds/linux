#!/bin/sh -
#
# $FreeBSD$
#
# Run nightly periodic scripts
#
# usage: periodic { daily | weekly | monthly | security } - run standard scripts
#        periodic /absolute/path/to/directory  - run periodic scripts in dir
#

usage () {
    echo "usage: $0 <directory of files to execute>" 1>&2
    echo "or     $0 { daily | weekly | monthly | security }"    1>&2
    exit 1
}

output_pipe()
{
    # Where's our output going ?
    eval output=\$${1##*/}_output
    case "$output" in
    /*) pipe="cat >>$output";;
    "") pipe=cat;;
    *)  pipe="mail -E -s '$host ${2}${2:+ }${1##*/} run output' $output";;
    esac
    eval $pipe
}

if [ $# -lt 1 ] ; then
    usage
fi

# If possible, check the global system configuration file,
# to see if there are additional dirs to check
if [ -r /etc/defaults/periodic.conf ]; then
    . /etc/defaults/periodic.conf
    source_periodic_confs
fi

host=`hostname`
export host

# If we were called normally, then create a lock file for each argument
# in turn and reinvoke ourselves with the LOCKED argument.  This prevents
# very long running jobs from being overlapped by another run as this is
# will lead the system running progressivly slower and more and more jobs
# are run at once.
if [ $1 != "LOCKED" ]; then
    ret=0
    for arg; do
        lockfile=/var/run/periodic.${arg##*/}.lock
        lockf -t 0 "${lockfile}" /bin/sh $0 LOCKED "$arg"
        case $? in
        0) ;;
        73) #EX_CANTCREATE
            echo "can't create ${lockfile}" | \
                output_pipe $arg "$PERIODIC"
            ret=1
            ;;
        75) #EX_TEMPFAIL
            echo "$host ${arg##*/} prior run still in progress" | \
                output_pipe $arg "$PERIODIC"
            ret=1
            ;;
        *)
            ret=1
            ;;
        esac
    done
    exit $ret
fi

if [ $# -ne 2 ]; then
    usage
fi
shift
arg=$1

if [ -z "$PERIODIC_ANTICONGESTION_FILE" ] ; then
	export PERIODIC_ANTICONGESTION_FILE=`mktemp ${TMPDIR:-/tmp}/periodic.anticongestion.XXXXXXXXXX`
fi
if tty > /dev/null 2>&1; then
	export PERIODIC_IS_INTERACTIVE=1
fi
tmp_output=`mktemp ${TMPDIR:-/tmp}/periodic.XXXXXXXXXX`
context="$PERIODIC"
export PERIODIC="$arg${PERIODIC:+ }${PERIODIC}"

# Execute each executable file in the directory list.  If the x bit is not
# set, assume the user didn't really want us to muck with it (it's a
# README file or has been disabled).

success=YES info=YES badconfig=NO empty_output=YES	# Defaults when ${run}_* aren't YES/NO
for var in success info badconfig empty_output; do
    case $(eval echo "\$${arg##*/}_show_$var") in
    [Yy][Ee][Ss]) eval $var=YES;;
    [Nn][Oo])     eval $var=NO;;
    esac
done

case $arg in
/*) if [ -d "$arg" ]; then
        dirlist="$arg"
    else
        echo "$0: $arg not found" >&2
        exit 1
    fi
    ;;
*)  dirlist=
    for top in /etc/periodic ${local_periodic}; do
        [ -d $top/$arg ] && dirlist="$dirlist $top/$arg"
    done
    ;;
esac

{
    empty=TRUE
    processed=0
    for dir in $dirlist; do
        for file in $dir/*; do
            if [ -x $file -a ! -d $file ]; then
                output=TRUE
                processed=$(($processed + 1))
                $file </dev/null >$tmp_output 2>&1
                rc=$?
                if [ -s $tmp_output ]; then
                    case $rc in
                    0)  [ $success = NO ] && output=FALSE;;
                    1)  [ $info = NO ] && output=FALSE;;
                    2)  [ $badconfig = NO ] && output=FALSE;;
                    esac
                    [ $output = TRUE ] && { cat $tmp_output; empty=FALSE; }
                fi
                cp /dev/null $tmp_output
            fi
        done
    done
    if [ $empty = TRUE ]; then
        if [ $empty_output = TRUE ]; then
            [ $processed = 1 ] && plural= || plural=s
            echo "No output from the $processed file$plural processed"
        fi
    else
        echo ""
        echo "-- End of $arg output --"
    fi
} | output_pipe $arg "$context"

rm -f $tmp_output
rm -f $PERIODIC_ANTICONGESTION_FILE
