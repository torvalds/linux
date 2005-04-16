#ifndef SSCAPE_IOCTL_H
#define SSCAPE_IOCTL_H


struct sscape_bootblock
{
  unsigned char code[256];
  unsigned version;
};

#define SSCAPE_MICROCODE_SIZE  65536

struct sscape_microcode
{
  unsigned char __user *code;
};

#define SND_SSCAPE_LOAD_BOOTB  _IOWR('P', 100, struct sscape_bootblock)
#define SND_SSCAPE_LOAD_MCODE  _IOW ('P', 101, struct sscape_microcode)

#endif
