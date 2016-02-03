#include "subcmd-config.h"

#define UNDEFINED "SUBCMD_HAS_NOT_BEEN_INITIALIZED"

struct subcmd_config subcmd_config = {
	.exec_name	= UNDEFINED,
	.prefix		= UNDEFINED,
	.exec_path	= UNDEFINED,
	.exec_path_env	= UNDEFINED,
	.pager_env	= UNDEFINED,
};
