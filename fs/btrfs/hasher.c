#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kerncompat.h"
#include "hash.h"

int main() {
	u64 result;
	int ret;
	char line[255];
	char *p;
	while(1) {
		p = fgets(line, 255, stdin);
		if (!p)
			break;
		if (strlen(line) == 0)
			continue;
		ret = btrfs_name_hash(line, strlen(line), &result);
		BUG_ON(ret);
		printf("hash returns %Lu\n", result);
	}
	return 0;
}
