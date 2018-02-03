#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2018 Jesper Dangaard Brouer, Red Hat Inc.
#
# Bash-shell example on using iproute2 tools 'tc' and 'ip' to load
# eBPF programs, both for XDP and clsbpf.  Shell script function
# wrappers and even long options parsing is illustrated, for ease of
# use.
#
# Related to sample/bpf/xdp2skb_meta_kern.c, which contains BPF-progs
# that need to collaborate between XDP and TC hooks.  Thus, it is
# convenient that the same tool load both programs that need to work
# together.
#
BPF_FILE=xdp2skb_meta_kern.o
DIR=$(dirname $0)

export TC=/usr/sbin/tc
export IP=/usr/sbin/ip

function usage() {
    echo ""
    echo "Usage: $0 [-vfh] --dev ethX"
    echo "  -d | --dev     :             Network device (required)"
    echo "  --flush        :             Cleanup flush TC and XDP progs"
    echo "  --list         : (\$LIST)     List TC and XDP progs"
    echo "  -v | --verbose : (\$VERBOSE)  Verbose"
    echo "  --dry-run      : (\$DRYRUN)   Dry-run only (echo commands)"
    echo ""
}

## -- General shell logging cmds --
function err() {
    local exitcode=$1
    shift
    echo "ERROR: $@" >&2
    exit $exitcode
}

function info() {
    if [[ -n "$VERBOSE" ]]; then
	echo "# $@"
    fi
}

## -- Helper function calls --

# Wrapper call for TC and IP
# - Will display the offending command on failure
function _call_cmd() {
    local cmd="$1"
    local allow_fail="$2"
    shift 2
    if [[ -n "$VERBOSE" ]]; then
	echo "$(basename $cmd) $@"
    fi
    if [[ -n "$DRYRUN" ]]; then
	return
    fi
    $cmd "$@"
    local status=$?
    if (( $status != 0 )); then
	if [[ "$allow_fail" == "" ]]; then
	    err 2 "Exec error($status) occurred cmd: \"$cmd $@\""
	fi
    fi
}
function call_tc() {
    _call_cmd "$TC" "" "$@"
}
function call_tc_allow_fail() {
    _call_cmd "$TC" "allow_fail" "$@"
}
function call_ip() {
    _call_cmd "$IP" "" "$@"
}

##  --- Parse command line arguments / parameters ---
# Using external program "getopt" to get --long-options
OPTIONS=$(getopt -o vfhd: \
    --long verbose,flush,help,list,dev:,dry-run -- "$@")
if (( $? != 0 )); then
    err 4 "Error calling getopt"
fi
eval set -- "$OPTIONS"

unset DEV
unset FLUSH
while true; do
    case "$1" in
	-d | --dev ) # device
	    DEV=$2
	    info "Device set to: DEV=$DEV" >&2
	    shift 2
	    ;;
	-v | --verbose)
	    VERBOSE=yes
	    # info "Verbose mode: VERBOSE=$VERBOSE" >&2
	    shift
	    ;;
	--dry-run )
	    DRYRUN=yes
	    VERBOSE=yes
	    info "Dry-run mode: enable VERBOSE and don't call TC+IP" >&2
	    shift
            ;;
	-f | --flush )
	    FLUSH=yes
	    shift
	    ;;
	--list )
	    LIST=yes
	    shift
	    ;;
	-- )
	    shift
	    break
	    ;;
	-h | --help )
	    usage;
	    exit 0
	    ;;
	* )
	    shift
	    break
	    ;;
    esac
done

FILE="$DIR/$BPF_FILE"
if [[ ! -e $FILE ]]; then
    err 3 "Missing BPF object file ($FILE)"
fi

if [[ -z $DEV ]]; then
    usage
    err 2 "Please specify network device -- required option --dev"
fi

## -- Function calls --

function list_tc()
{
    local device="$1"
    shift
    info "Listing current TC ingress rules"
    call_tc filter show dev $device ingress
}

function list_xdp()
{
    local device="$1"
    shift
    info "Listing current XDP device($device) setting"
    call_ip link show dev $device | grep --color=auto xdp
}

function flush_tc()
{
    local device="$1"
    shift
    info "Flush TC on device: $device"
    call_tc_allow_fail filter del dev $device ingress
    call_tc_allow_fail qdisc del dev $device clsact
}

function flush_xdp()
{
    local device="$1"
    shift
    info "Flush XDP on device: $device"
    call_ip link set dev $device xdp off
}

function attach_tc_mark()
{
    local device="$1"
    local file="$2"
    local prog="tc_mark"
    shift 2

    # Re-attach clsact to clear/flush existing role
    call_tc_allow_fail qdisc del dev $device clsact 2> /dev/null
    call_tc            qdisc add dev $device clsact

    # Attach BPF prog
    call_tc filter add dev $device ingress \
	    prio 1 handle 1 bpf da obj $file sec $prog
}

function attach_xdp_mark()
{
    local device="$1"
    local file="$2"
    local prog="xdp_mark"
    shift 2

    # Remove XDP prog in-case it's already loaded
    # TODO: Need ip-link option to override/replace existing XDP prog
    flush_xdp $device

    # Attach XDP/BPF prog
    call_ip link set dev $device xdp obj $file sec $prog
}

if [[ -n $FLUSH ]]; then
    flush_tc  $DEV
    flush_xdp $DEV
    exit 0
fi

if [[ -n $LIST ]]; then
    list_tc  $DEV
    list_xdp $DEV
    exit 0
fi

attach_tc_mark  $DEV $FILE
attach_xdp_mark $DEV $FILE
