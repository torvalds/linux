#!/bin/bash

GITHUB_RELEASES_PAGE="https://api.github.com/repos/frank-w/BPI-R2-4.14/releases/latest"

if ! [ -x "$(command -v jq)" ]; then
    echo "Please install jq (https://stedolan.github.io/jq/download/)";
    exit 1;
fi

if ! [ -x "$(command -v curl)" ]; then
    echo "Please install curl";
    exit 1;
fi
#ASSETS_URL=$(curl $GITHUB_RELEASES_PAGE | jq --raw-output '.assets_url');
#ASSETS_FILE=/tmp/assets.json
#curl $ASSETS_URL -o $ASSETS_FILE
curl https://api.github.com/repos/frank-w/BPI-R2-4.14/releases -o /tmp/releases.json

#FILES=()
#i=0;
#while true;
#do
#	# .deb file
#	FILE_URL=$(cat $ASSETS_FILE | jq --raw-output ".[$i].browser_download_url");
#	if [[ "$FILE_URL" != "null" ]];then
#		FILE_NAME=$(cat $ASSETS_FILE | jq --raw-output ".[$i].name");
#		FILE_CHG=$(cat $ASSETS_FILE | jq --raw-output ".[$i].updated_at");
#		#FILES+=("$FILE_NAME,$FILE_CHG,$FILE_URL");
#		FILES+=("$FILE_URL");
#		echo "[$i] $FILE_NAME ($FILE_CHG)"
#		i=$(($i+1))
#	else
#		break;
#	fi
#done

i=0
CUR_KERNEL=$(uname -r | sed -e 's/^\([0-9]\.[0-9]*\)\..*$/\1/')
#B=4.19-rc
B=4.14-main
while true;
do
	BRANCH=$(cat /tmp/releases.json | jq --raw-output ".[$i].name")
	if [[ "$BRANCH" != "null" ]];then
		#echo $BRANCH
		if [[ "$BRANCH" =~ $B.* ]]; then
			REL_DATE=$(cat /tmp/releases.json | jq --raw-output ".[$i].created_at")
			echo "[test] show release #$i ($BRANCH $REL_DATE)"
		fi
		i=$(($i+1))
	else
		break;
	fi
done

#echo "[x] exit"

#read -p "choice: " -n1 choice;
#echo
#case $choice in
#	[0-9]*)
#		val=${FILES[$choice]}
#		#set -f # for str-split avoid globbing (expansion of *).
#		#string=1,2,3,4
#		#array=(${string//,/ })
#		#array=($(echo $t | tr ',' ' '))
#		#echo ${array[2]}
#		#date "+%d.%m.%Y %H:%M" -d "2018-08-17T09:36:05Z"
#		curl -L $val -O
#	;;
#	x) exit
#	;;
#esac
