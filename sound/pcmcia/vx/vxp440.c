#define COMPILE_VXP440

/*
 add the following as /etc/pcmcia/vxp440.conf:

  device "snd-vxp440"
    class "audio" module "snd-vxp440"

  card "Digigram VX-POCKET440"
    manfid 0x01f1, 0x0100
    bind "snd-vxp440"
*/

#include "vxpocket.c"
