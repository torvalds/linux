#!/bin/sh
#-
# ----------------------------------------------------------------------------
# "THE BEER-WARE LICENSE" (Revision 42):
# <erdgeist@erdgeist.org> wrote this file. As long as you retain this notice you
# can do whatever you want with this stuff. If we meet some day, and you think
# this stuff is worth it, you can buy me a beer in return Poul-Henning Kamp
# ----------------------------------------------------------------------------
#
# $FreeBSD$
#

# define our bail out shortcut
exerr () { echo -e "Error: $*" >&2 ; exit 1; }
print_syntax () { echo -e "Syntax: $0 scan [-d device] [-n node]"; exit 1; }

main() {
unset node device started bdaddresses retry

# Only one command at the moment is scan (+ add)
[ "$#" -eq 1 -a "$1" = "scan" ] || print_syntax
shift

# Get command line options
while getopts :d:n: arg; do
	case ${arg} in
		d) device="$OPTARG";;
		n) node="$OPTARG";;
		?) print_syntax;;
	esac
done

# No use running without super user rights
if [ $( id -u ) -ne 0 ]; then
	exerr "$0 must modify files that belong to root.  Re-run as root."
fi

known_nodes=$( /usr/sbin/hccontrol read_node_list 2>/dev/null |\
	/usr/bin/tail -n +2 | /usr/bin/cut -d ' ' -f 1 )

# Check if netgraph knows about any HCI nodes
if ! [ "${known_nodes}" ]; then
	ng_nodes=$( /usr/sbin/ngctl list 2>/dev/null | \
		/usr/bin/grep -o "Name: .* Type: ubt" |/usr/bin/cut -d' ' -f2 )

	[ "${ng_nodes}" ] || exerr "No Bluetooth host controllers found."

	unset found
	for n in ${ng_nodes}; do
		if [ "${n}" = "${node%hci}" ]; then
			# Found the node but its stack is not set up? Do it now.
			/usr/sbin/service bluetooth start ${node%hci} || exit 1
			found="YES"
		fi
	done

	# If we have Bluetooth controller nodes without a set up stack,
	# ask the user if we shall start it up
	if ! [ "${found}" ]; then
		printf "No usable Bluetooth host controllers were found.\n"
		printf "These host controllers exist in the system:\n"
		printf "  %s\n" "${ng_nodes}"
		prompt="Choose a host controller to set up: [${ng_nodes%% *}]"
		read -p "${prompt}" node
		: ${node:="${ng_nodes%% *}"}
		/usr/sbin/service bluetooth start ${node} || exit 1
	fi

	# Re-read known nodes
	known_nodes=$(/usr/sbin/hccontrol read_node_list 2>/dev/null |
		/usr/bin/tail -n +2 | /usr/bin/cut -d ' ' -f 1 )

	# check if we succeeded in bringing it up
	[ "${known_nodes}" ] || exerr "Failed to set up Bluetooth stack"
fi

# if a node was requested on command line, check if it is there
if [ "${node}" ]; then
	unset found
	for n in ${known_nodes}; do
		[ "${n}" = "${node}" ] && found="YES"
		[ "${n}" = "${node}hci" ] && node="${node}hci" && found="YES"
	done
	[ "${found}" ] || exerr "Node ${node} not found"
fi

[ "${node}" ] && node="-n ${node}"

while ! [ "${bdaddresses}" ]; do
	retry=X${retry}
	printf "Scanning for new Bluetooth devices (Attempt %d of 5) ... " \
		${#retry}
	bdaddresses=$( /usr/sbin/hccontrol -N ${node} inquiry 2>/dev/null |
		/usr/bin/grep -o "BD_ADDR: .*" | /usr/bin/cut -d ' ' -f 2 )

	# Count entries and, if a device was requested on command line,
	# try to find it
	unset found count
	for bdaddress in ${bdaddresses}; do
		count=X${count}
		if [ "${bdaddress}" = "${device}" ]; then
			found=YES
			bdaddresses="${device}"
			count=X
			break
		fi
	done

	# If device was requested on command line but is not found,
	# or no devices found at all, rescan until retry is exhausted
	if ! [ "${found}" -o "${count}" -a -z "${device}" ]; then
		printf "failed.\n"
		if [ "${#retry}" -eq 5 ]; then
			[ "${device}" ] && exerr "Device ${device} not found"
			exerr "No new Bluetooth devices found"
		fi
		unset bdaddresses
		sleep 2
		continue
	fi

	[ ${#count} -gt 1 ] && plural=s || plural=''
	printf "done.\nFound %d new bluetooth device%s " ${#count} ${plural}
	printf "(now scanning for names):\n"

	# Looping again for the faster feedback
	unset count
	for bdaddress in ${bdaddresses}; do
		count=X${count}
		bdname=$( /usr/bin/bthost -b "${bdaddress}" 2>/dev/null )
		friendlyname=$( /usr/sbin/hccontrol Remote_Name_Request \
			${bdaddress} 2> /dev/null |
			/usr/bin/grep -o "Name: .*" |/usr/bin/cut -d ' ' -f 2- )

		# sdpcontrol should be able to pull vendor + product id via sdp
		printf "[%2d] %s\t\"%s\" (%s)\n" ${#count} "${bdaddress}" \
			"${friendlyname}" "${bdname}"

		eval bdaddress_${#count}=\${bdaddress}
		eval bdname_${#count}=\${bdname}
		eval friendlyname_${#count}=\${friendlyname}
	done

	# If a device was pre-selected, do not query the user
	[ "${device}" ] && topair=1 || unset topair

	# Even if only one device was found, user may chose 0 to rescan
	while ! [ "${topair}" ]; do
		prompt="Select device to pair with [1"
		[ ${#count} -gt 1 ] && prompt="${prompt}-${#count}"
		read -p "${prompt}, or 0 to rescan]: " topair
		if ! [ "${topair}" -ge 0 -a "${topair}" -le "${#count}" ] \
			2>/dev/null ; then
			printf "Value out of range: %s.\n" {topair}
			unset topair
		fi
	done

	[ "${topair}" -eq "0" ] && unset bdaddresses retry
done

eval bdaddress=\${bdaddress_${topair}}
eval bdname=\${bdname_${topair}}
eval friendlyname=\${friendlyname_${topair}}

# Do we need to add an entry to /etc/bluetooth/hosts?
if ! [ "${bdname}" ]; then
	printf "\nAdding device ${bdaddress} to /etc/bluetooth/hosts.\n"

	while ! [ "${bdname}" ]; do
		read -p "Enter friendly name. [${friendlyname}]: " _r
		: ${_r:="${friendlyname}"}

		if [ "${_r}" ]; then
			# Remove white space and non-friendly characters
			bdname=$( printf "%s" "${_r}" | tr -c '[:alnum:]-,.' _ )
			if [ "${_r}" != "${bdname}" ]; then
				printf "Notice: Using sanitized name"
				printf "\"%s\" in /etc/bluetooth/hosts.\n" \
					"${bdname}"
			fi
		fi
	done

	printf "%s\t%s\n" "${bdaddress}" "${bdname}" >> /etc/bluetooth/hosts
fi

# If scanning for the name did not succeed, resort to bdname
: ${friendlyname:="${bdname}"}

# now over to hcsecd

# Since hcsecd does not allow querying for known devices, we need to
# check for bdaddr entries manually.
#
# Also we cannot really modify the PIN in an existing entry. So we
# need to prompt the user to manually do it and restart this script
if ! /usr/sbin/service hcsecd enabled; then
	printf "\nWarning: hcsecd is not enabled.\n"
	printf "This daemon manages pairing requests.\n"
	read -p "Enable hcsecd? [yes]: " _r
	case "${_r}" in
		no|n|NO|N|No|nO) ;;
		*) /usr/sbin/service hcsecd enable;;
	esac
fi

secd_config=$( /usr/sbin/sysrc -n hcsecd_config )
secd_entries=$( /usr/bin/grep -Eo "bdaddr[[:space:]]+(${bdaddress}|${bdname})" \
	${secd_config} | awk '{ print $2; }' )

if [ "${secd_entries}" ]; then
	printf "\nWarning: An entry for device %s is already present in %s.\n" \
		${secd_entries} ${secd_config}
	printf "To modify pairing information, edit this file and run\n"
	printf "  service hcsecd restart\n"
	read -p "Continue? [yes]: " _r
	case "${_r}" in no|n|NO|N|No|nO) exit;; esac
else
	printf "\nWriting pairing information description block to %s.\n" \
		${secd_config}
	printf "(To get PIN, put device in pairing mode first.)\n"
	read -p "Enter PIN [nopin]: " pin
	[ "${pin}" ] && pin=\""${pin}"\" || pin="nopin"

	# Write out new hcsecd config block
	printf "\ndevice {\n\tbdaddr\t%s;\n\tname\t\"%s\";\n\tkey\tnokey\;\n\tpin\t%s\;\n}\n" \
		"${bdaddress}" "${friendlyname}" "${pin}" >> ${secd_config}

	# ... and make daemon reload config
	# TODO: hcsecd should provide a reload hook
	/usr/sbin/service hcsecd onerestart

	# TODO: we should check if hcsecd succeeded pairing and revert to an
	# old version of hcsecd.conf so we can undo adding the block above and
	# retry with a new PIN
	# also, if there's a way to force devices to re-pair, try this
fi

# now check for specific services to be provided by the device
# first up: HID

/usr/sbin/sdpcontrol -a "${bdaddress}" search HID | \
	/usr/bin/grep -q "^Record Handle: " || exit 0

printf "\nThis device provides human interface device services.\n"
read -p "Set it up? [yes]: " _r
case "${_r}" in
	no|n|NO|N|No|nO) exit 0;;
	*);;
esac

# Here we have found an HID and were asked to set it up
# NOTE: look out for the two exit 0 above if you extend this script

if ! /usr/sbin/service bthidd enabled; then
	printf "\nWarning: bthidd is not enabled."
	printf "\nThis daemon manages Bluetooth HID devices.\n"
	read -p "Enable bthidd? [yes]: " _r
	case "${_r}" in
		no|n|NO|N|No|nO) ;;
		 *) /usr/sbin/service bthidd enable;;
	esac
fi

# Check if bthidd already knows about this device
bthidd_known=$( /usr/sbin/bthidcontrol -a "${bdaddress}" known | \
	/usr/bin/grep "${bdaddress}" )

if [ "${bthidd_known}" ]; then
	printf "Notice: Device %s already known to bthidd.\n" "${bdaddress}"
	return 0
fi

bthidd_config=$( /usr/sbin/sysrc -n bthidd_config )
printf "Writing HID descriptor block to %s ... " "${bthidd_config}"
/usr/sbin/bthidcontrol -a "${bdaddress}" query >> "${bthidd_config}"

# Re-read config to see if we succeeded adding the device
bthidd_known=$( /usr/sbin/bthidcontrol -a "${bdaddress}" known | \
	grep "${bdaddress}" )
if ! [ "${bthidd_known}" ]; then
	printf "failed.\n"
else
	printf "success.\nTo re-read its config, bthidd must be restarted.\n"
	printf "Warning: If a Bluetooth keyboard is being used, the connection"
	printf "might be lost.\n"
	printf "It can be manually restarted later with\n"
	printf " service bthidd restart\n"
	read -p "Restart bthidd now? [yes]: " _r
	case "${_r}" in
		no|n|NO|N|No|nO) ;;
		*) /usr/sbin/service bthidd onerestart;;
	esac
fi

}

# After function definitions, main() can use them
main "$@"
exit 0

# TODO
# * If device is a keyboard, offer a text entry test field and if it does
#   not succeed, leave some clues for debugging (i.e. if the node responds
#   to pings, maybe switch keyboard on/off, etc)
# * Same if device is a mouse, i.e. hexdump /dev/sysmouse.
# * If device offers DUN profiles, ask the user if an entry in
#   /etc/ppp/ppp.conf should be created
# * If OPUSH or SPP is offered, refer to the respective man pages to give
#   some clues how to continue
