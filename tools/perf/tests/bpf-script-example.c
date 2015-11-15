#ifndef LINUX_VERSION_CODE
# error Need LINUX_VERSION_CODE
# error Example: for 4.2 kernel, put 'clang-opt="-DLINUX_VERSION_CODE=0x40200" into llvm section of ~/.perfconfig'
#endif
#define BPF_ANY 0
#define BPF_MAP_TYPE_ARRAY 2
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2

static void *(*bpf_map_lookup_elem)(void *map, void *key) =
	(void *) BPF_FUNC_map_lookup_elem;
static void *(*bpf_map_update_elem)(void *map, void *key, void *value, int flags) =
	(void *) BPF_FUNC_map_update_elem;

struct bpf_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
};

#define SEC(NAME) __attribute__((section(NAME), used))
struct bpf_map_def SEC("maps") flip_table = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 1,
};

SEC("func=sys_epoll_pwait")
int bpf_func__sys_epoll_pwait(void *ctx)
{
	int ind =0;
	int *flag = bpf_map_lookup_elem(&flip_table, &ind);
	int new_flag;
	if (!flag)
		return 0;
	/* flip flag and store back */
	new_flag = !*flag;
	bpf_map_update_elem(&flip_table, &ind, &new_flag, BPF_ANY);
	return new_flag;
}
char _license[] SEC("license") = "GPL";
int _version SEC("version") = LINUX_VERSION_CODE;
