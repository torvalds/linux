#
# Common functions used by pktgen scripts
#  - Depending on bash 3 (or higher) syntax
#
# Author: Jesper Dangaaard Brouer
# License: GPL

## -- General shell logging cmds --
function err() {
    local exitcode=$1
    shift
    echo "ERROR: $@" >&2
    exit $exitcode
}

function warn() {
    echo "WARN : $@" >&2
}

function info() {
    if [[ -n "$VERBOSE" ]]; then
	echo "INFO : $@" >&2
    fi
}

## -- Pktgen proc config commands -- ##
export PROC_DIR=/proc/net/pktgen
#
# Three different shell functions for configuring the different
# components of pktgen:
#   pg_ctrl(), pg_thread() and pg_set().
#
# These functions correspond to pktgens different components.
# * pg_ctrl()   control "pgctrl" (/proc/net/pktgen/pgctrl)
# * pg_thread() control the kernel threads and binding to devices
# * pg_set()    control setup of individual devices
function pg_ctrl() {
    local proc_file="pgctrl"
    proc_cmd ${proc_file} "$@"
}

function pg_thread() {
    local thread=$1
    local proc_file="kpktgend_${thread}"
    shift
    proc_cmd ${proc_file} "$@"
}

function pg_set() {
    local dev=$1
    local proc_file="$dev"
    shift
    proc_cmd ${proc_file} "$@"
}

# More generic replacement for pgset(), that does not depend on global
# variable for proc file.
function proc_cmd() {
    local result
    local proc_file=$1
    # after shift, the remaining args are contained in $@
    shift
    local proc_ctrl=${PROC_DIR}/$proc_file
    if [[ ! -e "$proc_ctrl" ]]; then
	err 3 "proc file:$proc_ctrl does not exists (dev added to thread?)"
    else
	if [[ ! -w "$proc_ctrl" ]]; then
	    err 4 "proc file:$proc_ctrl not writable, not root?!"
	fi
    fi

    if [[ "$DEBUG" == "yes" ]]; then
	echo "cmd: $@ > $proc_ctrl"
    fi
    # Quoting of "$@" is important for space expansion
    echo "$@" > "$proc_ctrl"
    local status=$?

    result=$(grep "Result: OK:" $proc_ctrl)
    # Due to pgctrl, cannot use exit code $? from grep
    if [[ "$result" == "" ]]; then
	grep "Result:" $proc_ctrl >&2
    fi
    if (( $status != 0 )); then
	err 5 "Write error($status) occurred cmd: \"$@ > $proc_ctrl\""
    fi
}

# Old obsolete "pgset" function, with slightly improved err handling
function pgset() {
    local result

    if [[ "$DEBUG" == "yes" ]]; then
	echo "cmd: $1 > $PGDEV"
    fi
    echo $1 > $PGDEV
    local status=$?

    result=`cat $PGDEV | fgrep "Result: OK:"`
    if [[ "$result" == "" ]]; then
         cat $PGDEV | fgrep Result:
    fi
    if (( $status != 0 )); then
	err 5 "Write error($status) occurred cmd: \"$1 > $PGDEV\""
    fi
}

## -- General shell tricks --

function root_check_run_with_sudo() {
    # Trick so, program can be run as normal user, will just use "sudo"
    #  call as root_check_run_as_sudo "$@"
    if [ "$EUID" -ne 0 ]; then
	if [ -x $0 ]; then # Directly executable use sudo
	    info "Not root, running with sudo"
            sudo "$0" "$@"
            exit $?
	fi
	err 4 "cannot perform sudo run of $0"
    fi
}
