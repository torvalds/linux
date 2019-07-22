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

    # 0 < port < 65536
    if [[ $min_port -gt 0 && $min_port -lt 65536 ]]; then
	if [[ $max_port -gt 0 && $max_port -lt 65536 ]]; then
	    if [[ $min_port -le $max_port ]]; then
		return 0
	    fi
	fi
    fi

    err 5 "Invalid port(s): $min_port-$max_port"
}
