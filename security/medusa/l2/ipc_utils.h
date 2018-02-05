struct kern_ipc_perm * medusa_ipc_info_lock(int id, unsigned int ipc_class);
void medusa_ipc_info_unlock(struct kern_ipc_perm * ipcp);
