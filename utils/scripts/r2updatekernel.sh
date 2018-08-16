#!/bin/bash

GITHUB_RELEASES_PAGE="https://api.github.com/repos/RamonSmit/BPI-R2-4.14/releases/latest"

if ! [ -x "$(command -v jq)" ]; then
    echo "Please install jq (https://stedolan.github.io/jq/download/)";
    exit 1;
fi

if ! [ -x "$(command -v curl)" ]; then
    echo "Please install curl";
    exit 1;
fi
ASSETS_URL=$(curl $GITHUB_RELEASES_PAGE | jq --raw-output '.assets_url');
ASSETS_FILE=/tmp/assets.json
curl $ASSETS_URL -o $ASSETS_FILE

FILES=()
i=0;
while true;
do
	# .deb file
	FILE_URL=$(cat $ASSETS_FILE | jq --raw-output ".[$i].browser_download_url");
	if [[ "$FILE_URL" != "null" ]];then
	FILE_NAME=$(cat $ASSETS_FILE | jq --raw-output ".[$i].name");
	FILE_CHG=$(cat $ASSETS_FILE | jq --raw-output ".[$i].updated_at");
		#FILES+=("$FILE_NAME,$FILE_CHG,$FILE_URL");
		FILES+=("$FILE_URL");
		echo "[$i] $FILE_NAME ($FILE_CHG)"
		i=$(($i+1))
	else
		break;
	fi
done

read -p "choice: " -n1 choice;
echo
val=${FILES[$choice]}
curl -L $val -O

