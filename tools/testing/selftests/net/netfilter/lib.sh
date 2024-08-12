net_netfilter_dir=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "$net_netfilter_dir/../lib.sh"

checktool (){
	if ! $1 > /dev/null 2>&1; then
		echo "SKIP: Could not $2"
		exit $ksft_skip
	fi
}
