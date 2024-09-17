#
# Common functions used by pktgen scripts
#  - Depending on bash 3 (or higher) syntax
#
# Author: Jesper Dangaaard Brouer
# License: GPL

set -o errexit

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
    local status=0
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
    echo "$@" > "$proc_ctrl" || status=$?

    if [[ "$proc_file" != "pgctrl" ]]; then
        result=$(grep "Result: OK:" $proc_ctrl) || true
        if [[ "$result" == "" ]]; then
            grep "Result:" $proc_ctrl >&2
        fi
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

if [[ -z "$APPEND" ]]; then
	if [[ $EUID -eq 0 ]]; then
		# Cleanup pktgen setup on exit if thats not "append mode"
		trap 'pg_ctrl "reset"' EXIT
	fi
fi

## -- General shell tricks --

function root_check_run_with_sudo() {
    # Trick so, program can be run as normal user, will just use "sudo"
    #  call as root_check_run_as_sudo "$@"
    if [ "$EUID" -ne 0 ]; then
	if [ -x $0 ]; then # Directly executable use sudo
	    info "Not root, running with sudo"
            sudo -E "$0" "$@"
            exit $?
	fi
	err 4 "cannot perform sudo run of $0"
    fi
}

# Exact input device's NUMA node info
function get_iface_node()
{
    local node=$(</sys/class/net/$1/device/numa_node)
    if [[ $node == -1 ]]; then
        echo 0
    else
        echo $node
    fi
}

# Given an Dev/iface, get its queues' irq numbers
function get_iface_irqs()
{
	local IFACE=$1
	local queues="${IFACE}-.*TxRx"

	irqs=$(grep "$queues" /proc/interrupts | cut -f1 -d:)
	[ -z "$irqs" ] && irqs=$(grep $IFACE /proc/interrupts | cut -f1 -d:)
	[ -z "$irqs" ] && irqs=$(for i in `ls -Ux /sys/class/net/$IFACE/device/msi_irqs` ;\
	    do grep "$i:.*TxRx" /proc/interrupts | grep -v fdir | cut -f 1 -d : ;\
	    done)
	[ -z "$irqs" ] && err 3 "Could not find interrupts for $IFACE"

	echo $irqs
}

# Given a NUMA node, return cpu ids belonging to it.
function get_node_cpus()
{
	local node=$1
	local node_cpu_list
	local node_cpu_range_list=`cut -f1- -d, --output-delimiter=" " \
	                  /sys/devices/system/node/node$node/cpulist`

	for cpu_range in $node_cpu_range_list
	do
	    node_cpu_list="$node_cpu_list "`seq -s " " ${cpu_range//-/ }`
	done

	echo $node_cpu_list
}

# Check $1 is in between $2, $3 ($2 <= $1 <= $3)
function in_between() { [[ ($1 -ge $2) && ($1 -le $3) ]] ; }

# Extend shrunken IPv6 address.
# fe80::42:bcff:fe84:e10a => fe80:0:0:0:42:bcff:fe84:e10a
function extend_addr6()
{
    local addr=$1
    local sep=: sep2=::
    local sep_cnt=$(tr -cd $sep <<< $1 | wc -c)
    local shrink

    # separator count should be (2 <= $sep_cnt <= 7)
    if ! (in_between $sep_cnt 2 7); then
        err 5 "Invalid IP6 address: $1"
    fi

    # if shrink '::' occurs multiple, it's malformed.
    shrink=( $(egrep -o "$sep{2,}" <<< $addr) )
    if [[ ${#shrink[@]} -ne 0 ]]; then
        if [[ ${#shrink[@]} -gt 1 || ( ${shrink[0]} != $sep2 ) ]]; then
            err 5 "Invalid IP6 address: $1"
        fi
    fi

    # add 0 at begin & end, and extend addr by adding :0
    [[ ${addr:0:1} == $sep ]] && addr=0${addr}
    [[ ${addr: -1} == $sep ]] && addr=${addr}0
    echo "${addr/$sep2/$(printf ':0%.s' $(seq $[8-sep_cnt])):}"
}

# Given a single IP(v4/v6) address, whether it is valid.
function validate_addr()
{
    # check function is called with (funcname)6
    [[ ${FUNCNAME[1]: -1} == 6 ]] && local IP6=6
    local bitlen=$[ IP6 ? 128 : 32 ]
    local len=$[ IP6 ? 8 : 4 ]
    local max=$[ 2**(len*2)-1 ]
    local net prefix
    local addr sep

    IFS='/' read net prefix <<< $1
    [[ $IP6 ]] && net=$(extend_addr6 $net)

    # if prefix exists, check (0 <= $prefix <= $bitlen)
    if [[ -n $prefix ]]; then
        if ! (in_between $prefix 0 $bitlen); then
            err 5 "Invalid prefix: /$prefix"
        fi
    fi

    # set separator for each IP(v4/v6)
    [[ $IP6 ]] && sep=: || sep=.
    IFS=$sep read -a addr <<< $net

    # array length
    if [[ ${#addr[@]} != $len ]]; then
        err 5 "Invalid IP$IP6 address: $1"
    fi

    # check each digit (0 <= $digit <= $max)
    for digit in "${addr[@]}"; do
        [[ $IP6 ]] && digit=$[ 16#$digit ]
        if ! (in_between $digit 0 $max); then
            err 5 "Invalid IP$IP6 address: $1"
        fi
    done

    return 0
}

function validate_addr6() { validate_addr $@ ; }

# Given a single IP(v4/v6) or CIDR, return minimum and maximum IP addr.
function parse_addr()
{
    # check function is called with (funcname)6
    [[ ${FUNCNAME[1]: -1} == 6 ]] && local IP6=6
    local net prefix
    local min_ip max_ip

    IFS='/' read net prefix <<< $1
    [[ $IP6 ]] && net=$(extend_addr6 $net)

    if [[ -z $prefix ]]; then
        min_ip=$net
        max_ip=$net
    else
        # defining array for converting Decimal 2 Binary
        # 00000000 00000001 00000010 00000011 00000100 ...
        local d2b='{0..1}{0..1}{0..1}{0..1}{0..1}{0..1}{0..1}{0..1}'
        [[ $IP6 ]] && d2b+=$d2b
        eval local D2B=($d2b)

        local bitlen=$[ IP6 ? 128 : 32 ]
        local remain=$[ bitlen-prefix ]
        local octet=$[ IP6 ? 16 : 8 ]
        local min_mask max_mask
        local min max
        local ip_bit
        local ip sep

        # set separator for each IP(v4/v6)
        [[ $IP6 ]] && sep=: || sep=.
        IFS=$sep read -ra ip <<< $net

        min_mask="$(printf '1%.s' $(seq $prefix))$(printf '0%.s' $(seq $remain))"
        max_mask="$(printf '0%.s' $(seq $prefix))$(printf '1%.s' $(seq $remain))"

        # calculate min/max ip with &,| operator
        for i in "${!ip[@]}"; do
            digit=$[ IP6 ? 16#${ip[$i]} : ${ip[$i]} ]
            ip_bit=${D2B[$digit]}

            idx=$[ octet*i ]
            min[$i]=$[ 2#$ip_bit & 2#${min_mask:$idx:$octet} ]
            max[$i]=$[ 2#$ip_bit | 2#${max_mask:$idx:$octet} ]
            [[ $IP6 ]] && { min[$i]=$(printf '%X' ${min[$i]});
                            max[$i]=$(printf '%X' ${max[$i]}); }
        done

        min_ip=$(IFS=$sep; echo "${min[*]}")
        max_ip=$(IFS=$sep; echo "${max[*]}")
    fi

    echo $min_ip $max_ip
}

function parse_addr6() { parse_addr $@ ; }

# Given a single or range of port(s), return minimum and maximum port number.
function parse_ports()
{
    local port_str=$1
    local port_list
    local min_port
    local max_port

    IFS="-" read -ra port_list <<< $port_str

    min_port=${port_list[0]}
    max_port=${port_list[1]:-$min_port}

    echo $min_port $max_port
}

# Given a minimum and maximum port, verify port number.
function validate_ports()
{
    local min_port=$1
    local max_port=$2

    # 1 <= port <= 65535
    if (in_between $min_port 1 65535); then
	if (in_between $max_port 1 65535); then
	    if [[ $min_port -le $max_port ]]; then
		return 0
	    fi
	fi
    fi

    err 5 "Invalid port(s): $min_port-$max_port"
}
