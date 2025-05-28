#include <test_progs.h>

#include "struct_ops_kptr_return.skel.h"
#include "struct_ops_kptr_return_fail__wrong_type.skel.h"
#include "struct_ops_kptr_return_fail__invalid_scalar.skel.h"
#include "struct_ops_kptr_return_fail__nonzero_offset.skel.h"
#include "struct_ops_kptr_return_fail__local_kptr.skel.h"

void test_struct_ops_kptr_return(void)
{
	RUN_TESTS(struct_ops_kptr_return);
	RUN_TESTS(struct_ops_kptr_return_fail__wrong_type);
	RUN_TESTS(struct_ops_kptr_return_fail__invalid_scalar);
	RUN_TESTS(struct_ops_kptr_return_fail__nonzero_offset);
	RUN_TESTS(struct_ops_kptr_return_fail__local_kptr);
}
