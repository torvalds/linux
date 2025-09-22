# $Id: test_wrapper.sh,v 1.2 2016/09/04 15:24:50 vgross Exp $

TESTIFACE="vether2323"
TESTNET=$(jot -r -s ':' -w %x 2 0 65535)
DESTADDR="fd00:${TESTNET}::100"
FIRSTADDR="fd00:${TESTNET}::1"
BASEADDR="fd00:${TESTNET}::2"
ADDR_3="fd00:${TESTNET}::3"
ADDR_4="fd00:${TESTNET}::4"
ABSENTADDR="fd00:${TESTNET}::5"

if ifconfig $TESTIFACE 2> /dev/null
then
	echo "Interface $TESTIFACE already exists, and this test will change its configuration"
	echo "Make sure this interface does not exist to run this test"
	echo "SKIPPED"
	exit
fi

if ! [[ -n ${PROG} && -x ${PROG} ]]
then
	echo "PROG not set or not an executable file"
	echo "SKIPPED"
	exit
fi

trap "${SUDO} ifconfig ${TESTIFACE} destroy" EXIT ERR HUP INT QUIT TERM

${SUDO} ifconfig ${TESTIFACE} inet6 ${DESTADDR}/64
${SUDO} ifconfig ${TESTIFACE} inet6 ${FIRSTADDR}/64
${SUDO} ifconfig ${TESTIFACE} inet6 ${BASEADDR}/64
${SUDO} ifconfig ${TESTIFACE} inet6 ${ADDR_3}/64
${SUDO} ifconfig ${TESTIFACE} inet6 ${ADDR_4}/64
sleep 1
set -ex
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${ADDR_3} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -m ${ADDR_3} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${BASEADDR} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -m ${BASEADDR} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${ABSENTADDR} -e 49
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -m ${ABSENTADDR} -e 49
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${FIRSTADDR} -e 48
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -m ${FIRSTADDR} -e 48
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b :: -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b :: -o ${ADDR_3} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b :: -m ${ADDR_3} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b :: -o ${FIRSTADDR} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b :: -m ${FIRSTADDR} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o :: -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -m :: -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${ADDR_3} -m ${ADDR_4} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${ABSENTADDR} -m ${ADDR_4} -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${ADDR_3} -m :: -e 0
${PROG} -d ${DESTADDR} -f ${FIRSTADDR} -b ${BASEADDR} -o ${FIRSTADDR} -m :: -e 0
${PROG} -d ${DESTADDR} -b :: -e 0
${PROG} -d ${DESTADDR} -b :: -o ${ADDR_3} -e 0
${PROG} -d ${DESTADDR} -b :: -m ${ADDR_3} -e 0
set +ex
