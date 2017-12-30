#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# test types can be passed on the command line:
#
# - control: any device can do this
# - out, in:  out needs 'bulk sink' firmware, in needs 'bulk src'
# - iso-out, iso-in:  out needs 'iso sink' firmware, in needs 'iso src'
# - halt: needs bulk sink+src, tests halt set/clear from host
# - unlink: needs bulk sink and/or src, test HCD unlink processing
# - loop: needs firmware that will buffer N transfers
#
# run it for hours, days, weeks.
#

#
# this default provides a steady test load for a bulk device
#
TYPES='control out in'
#TYPES='control out in halt'

#
# to test HCD code
#
#  - include unlink tests
#  - add some ${RANDOM}ness
#  - connect several devices concurrently (same HC)
#  - keep HC's IRQ lines busy with unrelated traffic (IDE, net, ...)
#  - add other concurrent system loads
#

declare -i COUNT BUFLEN

COUNT=50000
BUFLEN=2048

# NOTE:  the 'in' and 'out' cases are usually bulk, but can be
# set up to use interrupt transfers by 'usbtest' module options


if [ "$DEVICE" = "" ]; then
	echo "testing ALL recognized usbtest devices"
	echo ""
	TEST_ARGS="-a"
else
	TEST_ARGS=""
fi

do_test ()
{
    if ! ./testusb $TEST_ARGS -s $BUFLEN -c $COUNT $* 2>/dev/null
    then
	echo "FAIL"
	exit 1
    fi
}

ARGS="$*"

if [ "$ARGS" = "" ];
then
    ARGS="$TYPES"
fi

# FIXME use /sys/bus/usb/device/$THIS/bConfigurationValue to
# check and change configs

CONFIG=''

check_config ()
{
    if [ "$CONFIG" = "" ]; then
	CONFIG=$1
	echo "assuming $CONFIG configuration"
	return
    fi
    if [ "$CONFIG" = $1 ]; then
	return
    fi

    echo "** device must be in $1 config, but it's $CONFIG instead"
    exit 1
}


echo "TESTING:  $ARGS"

while : true
do
    echo $(date)

    for TYPE in $ARGS
    do
	# restore defaults
	COUNT=5000
	BUFLEN=2048

	# FIXME automatically multiply COUNT by 10 when
	# /sys/bus/usb/device/$THIS/speed == "480"

#	COUNT=50000

	case $TYPE in
	control)
	    # any device, in any configuration, can use this.
	    echo '** Control test cases:'

	    echo "test 9: ch9 postconfig"
	    do_test -t 9 -c 5000
	    echo "test 10: control queueing"
	    do_test -t 10 -c 5000

	    # this relies on some vendor-specific commands
	    echo "test 14: control writes"
	    do_test -t 14 -c 15000 -s 256 -v 1

	    echo "test 21: control writes, unaligned"
	    do_test -t 21 -c 100 -s 256 -v 1

	    ;;

	out)
	    check_config sink-src
	    echo '** Host Write (OUT) test cases:'

	    echo "test 1: $COUNT transfers, same size"
	    do_test -t 1
	    echo "test 3: $COUNT transfers, variable/short size"
	    do_test -t 3 -v 421

	    COUNT=100
	    echo "test 17: $COUNT transfers, unaligned DMA map by core"
	    do_test -t 17

	    echo "test 19: $COUNT transfers, unaligned DMA map by usb_alloc_coherent"
	    do_test -t 19

	    COUNT=2000
	    echo "test 5: $COUNT scatterlists, same size entries"
	    do_test -t 5

	    # try to trigger short OUT processing bugs
	    echo "test 7a: $COUNT scatterlists, variable size/short entries"
	    do_test -t 7 -v 579
	    BUFLEN=4096
	    echo "test 7b: $COUNT scatterlists, variable size/bigger entries"
	    do_test -t 7 -v 41
	    BUFLEN=64
	    echo "test 7c: $COUNT scatterlists, variable size/micro entries"
	    do_test -t 7 -v 63
	    ;;

	iso-out)
	    check_config sink-src
	    echo '** Host ISOCHRONOUS Write (OUT) test cases:'

	    # at peak iso transfer rates:
	    # - usb 2.0 high bandwidth, this is one frame.
	    # - usb 1.1, it's twenty-four frames.
	    BUFLEN=24500

	    COUNT=1000

# COUNT=10000

	    echo "test 15: $COUNT transfers, same size"
	    # do_test -t 15 -g 3 -v 0
	    BUFLEN=32768
	    do_test -t 15 -g 8 -v 0

	    # FIXME it'd make sense to have an iso OUT test issuing
	    # short writes on more packets than the last one

	    COUNT=100
	    echo "test 22: $COUNT transfers, non aligned"
	    do_test -t 22 -g 8 -v 0

	    ;;

	in)
	    check_config sink-src
	    echo '** Host Read (IN) test cases:'

	    # NOTE:  these "variable size" reads are just multiples
	    # of 512 bytes, no EOVERFLOW testing is done yet

	    echo "test 2: $COUNT transfers, same size"
	    do_test -t 2
	    echo "test 4: $COUNT transfers, variable size"
	    do_test -t 4

	    COUNT=100
	    echo "test 18: $COUNT transfers, unaligned DMA map by core"
	    do_test -t 18

	    echo "test 20: $COUNT transfers, unaligned DMA map by usb_alloc_coherent"
	    do_test -t 20

	    COUNT=2000
	    echo "test 6: $COUNT scatterlists, same size entries"
	    do_test -t 6
	    echo "test 8: $COUNT scatterlists, variable size entries"
	    do_test -t 8
	    ;;

	iso-in)
	    check_config sink-src
	    echo '** Host ISOCHRONOUS Read (IN) test cases:'

	    # at peak iso transfer rates:
	    # - usb 2.0 high bandwidth, this is one frame.
	    # - usb 1.1, it's twenty-four frames.
	    BUFLEN=24500

	    COUNT=1000

# COUNT=10000

	    echo "test 16: $COUNT transfers, same size"
	    # do_test -t 16 -g 3 -v 0
	    BUFLEN=32768
	    do_test -t 16 -g 8 -v 0

	    # FIXME since iso expects faults, it'd make sense
	    # to have an iso IN test issuing short reads ...

	    COUNT=100
	    echo "test 23: $COUNT transfers, unaligned"
	    do_test -t 23 -g 8 -v 0

	    ;;

	halt)
	    # NOTE:  sometimes hardware doesn't cooperate well with halting
	    # endpoints from the host side.  so long as mass-storage class
	    # firmware can halt them from the device, don't worry much if
	    # you can't make this test work on your device.
	    COUNT=2000
	    echo "test 13: $COUNT halt set/clear"
	    do_test -t 13
	    ;;

	unlink)
	    COUNT=2000
	    echo "test 11: $COUNT read unlinks"
	    do_test -t 11

	    echo "test 12: $COUNT write unlinks"
	    do_test -t 12
	    ;;

	loop)
	    # defaults need too much buffering for ez-usb devices
	    BUFLEN=2048
	    COUNT=32

	    # modprobe g_zero qlen=$COUNT buflen=$BUFLEN loopdefault
	    check_config loopback

	    # FIXME someone needs to write and merge a version of this

	    echo "write $COUNT buffers of $BUFLEN bytes, read them back"

	    echo "write $COUNT variable size buffers, read them back"

	    ;;

	*)
	    echo "Don't understand test type $TYPE"
	    exit 1;
	esac
	echo ''
    done
done

# vim: sw=4
