
int opl3_detect (int ioaddr, int *osp);
int opl3_init(int ioaddr, int *osp, struct module *owner);

void enable_opl3_mode(int left, int right, int both);
