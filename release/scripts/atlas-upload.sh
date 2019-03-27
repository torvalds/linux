#!/bin/sh
#-
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# Upload a Vagrant image to Hashicorp's Atlas service
#
# $FreeBSD$
#

ATLAS_API_URL=''
ATLAS_UPLOAD_URL='https://app.vagrantup.com'
DESCRIPTION="FreeBSD Snapshot Build"

usage() {
	echo "${0} usage:"
	echo "-b box-name -d 'box description' -f box-to-upload -k api-key -p provider -u user -v version"
	return 1
}

main () {
	while getopts "b:d:f:k:p:u:v:" arg; do
		case "${arg}" in
			b)
				BOX="${OPTARG}"
				;;
			d)
				DESCRIPTION="${OPTARG}"
				;;
			f)
				FILE="${OPTARG}"
				;;
			k)
				KEY="${OPTARG}"
				;;
			p)
				PROVIDER="${OPTARG}"
				;;
			u)
				USERNAME="${OPTARG}"
				;;
			v)
				VERSION="${OPTARG}"
				;;
			*)
				;;
		esac
	done

	if [ -z "${BOX}" -o \
		-z "${FILE}" -o \
		-z "${KEY}" -o \
		-z "${PROVIDER}" -o \
		-z "${USERNAME}" -o \
		-z "${VERSION}" ];
	then
		usage || exit 0
	fi

	# Check to see if the box exists or create it
	BOXRESULT=$(/usr/local/bin/curl -s "${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}?access_token=${KEY}")
	if [ $? != 0 ]; then
		echo "Failed to connect to the API"
		exit 2;
	fi
	echo $BOXRESULT | grep "\"name\":\"${BOX}\"" > /dev/null
	if [ $? != 0 ]; then
		echo "Creating box: ${BOX}"
		/usr/local/bin/curl -s ${ATLAS_UPLOAD_URL}/api/v1/boxes -X POST -d "box[name]=${BOX}" -d "access_token=${KEY}" > /dev/null
		/usr/local/bin/curl -s ${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX} -X PUT -d "box[is_private]=false" -d "access_token=${KEY}" > /dev/null
		/usr/local/bin/curl -s ${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX} -X PUT -d "box[description]='${DESCRIPTION}'" -d "access_token=${KEY}" > /dev/null
	else
		echo "Box already exists"
	fi

	# Check to see if the version exists or create it
	VERSIONRESULT=$(/usr/local/bin/curl -s "${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION}?access_token=${KEY}")
	if [ $? != 0 ]; then
		echo "Failed to connect to the API"
		exit 2;
	fi
	echo $VERSIONRESULT | grep "version/${VERSION}" > /dev/null
	if [ $? != 0 ]; then
		echo "Creating version: ${VERSION}"
		/usr/local/bin/curl -s ${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/versions -X POST -d "version[version]=${VERSION}" -d "access_token=${KEY}" > /dev/null
		/usr/local/bin/curl -s ${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION} -X PUT -d "version[description]=${DESCRIPTION}" -d "access_token=${KEY}" > /dev/null
		VERSIONRESULT=$(/usr/local/bin/curl -s "${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION}?access_token=${KEY}")
		echo $VERSIONRESULT | grep "version/${VERSION}" > /dev/null
		if [ $? != 0 ]; then
			echo "Failed to create version"
			exit 2
		fi
	else
		echo "Version already exists"
	fi

	# Check to see if the provider exists or create it
	PROVIDERRESULT=$(/usr/local/bin/curl -s "${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION}/provider/${PROVIDER}?access_token=${KEY}")
	if [ $? != 0 ]; then
		echo "Failed to connect to the API"
		exit 2;
	fi
	echo $PROVIDERRESULT | grep "provider/${PROVIDER}" > /dev/null
	if [ $? != 0 ]; then
		echo "Creating provider: ${PROVIDER}"
		/usr/local/bin/curl -s ${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION}/providers -X POST -d "provider[name]=${PROVIDER}" -d "access_token=${KEY}" > /dev/null
	else
		echo "Provider already exists"
	fi

	# Request an upload token
	TOKENRESULT=$(/usr/local/bin/curl -s "${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION}/provider/${PROVIDER}/upload?access_token=${KEY}")
	if [ $? != 0 ]; then
		echo "Failed to get the token from the API"
		exit 2;
	fi
	echo ${TOKENRESULT} | grep -E "upload_path" > /dev/null
	if [ $? != 0 ]; then
		echo "No token found from the API"
		exit 2
	else
		TOKEN=$(echo $TOKENRESULT | sed -e 's/.*token":"//' -e 's/.*upload_path":"//' -e 's/}$//g' -e 's/"//g')
		echo "Uploading to Atlas"
		UPLOADRESULT=$(/usr/local/bin/curl -s -X PUT --upload-file ${FILE} "${TOKEN}")

		# Validate the Upload
		echo "Validating"
		VALIDRESULT=$(/usr/local/bin/curl -s "${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION}/provider/${PROVIDER}?access_token=${KEY}")
		HOSTED_TOKEN=$(echo $VALIDRESULT | sed -e 's/.*"hosted"://' -e 's/,.*$//')
		if [ ! -z ${TOKEN} -a "${HOSTED_TOKEN}" != "true" ]; then
			echo "Upload failed, try again."
			exit 2
		fi

		# Release the version
		echo "Releasing ${VERSION} of ${BOX} in Atlas"
		/usr/local/bin/curl -s ${ATLAS_UPLOAD_URL}/api/v1/box/${USERNAME}/${BOX}/version/${VERSION}/release -X PUT -d "access_token=${KEY}" > /dev/null
	fi
}

main "$@"
