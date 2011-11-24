#!/bin/sh

## $Id: update-release-tag.sh 18839 2011-04-26 11:53:52Z johe $

getversion() {
    svnversion -nc ${1} | sed 's/.*://'
}

HOSTDRIVER_DIR=${1-.}
[ -d "${HOSTDRIVER_DIR}/kernel/ic" ] || { echo "$0: unexpected dir: ${HOSTDRIVER_DIR}" 1>&2; exit 1;}
[ -d "${HOSTDRIVER_DIR}/WiFiEngine/wifi_drv" ] || { echo "$0: unexpected dir: ${HOSTDRIVER_DIR}" 2>&1; exit 1;}

DRIVER_VER=$(getversion ${HOSTDRIVER_DIR})
WIFIENGINE_VER=$(getversion ${HOSTDRIVER_DIR}/WiFiEngine)

[ "${DRIVER_VER}" = exported ] && exit
[ "${WIFIENGINE_VER}" = exported ] && exit

RTAG="${HOSTDRIVER_DIR}/kernel/ic/linux_release_tag.h"

cat > "${RTAG}.new" <<EOF
#define LINUX_RELEASE_STRING "\$Release: ${DRIVER_VER} ${WIFIENGINE_VER} \$"
EOF

if cmp -s "${RTAG}" "${RTAG}.new"; then
    rm -f "${RTAG}.new"
else
    mv -f "${RTAG}" "${RTAG}~"
    mv -f "${RTAG}.new" "${RTAG}"
fi
