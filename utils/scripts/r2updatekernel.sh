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

# .deb file
ASSETS=$(cat $ASSETS_FILE | jq --raw-output '.[0].browser_download_url');
echo $ASSETS;

# .tar.gz
ASSETS=$(cat $ASSETS_FILE | jq --raw-output '.[1].browser_download_url');
echo $ASSETS;

# .tar.gz.md5
ASSETS=$(cat $ASSETS_FILE | jq --raw-output '.[2].browser_download_url');
echo $ASSETS;
