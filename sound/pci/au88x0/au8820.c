#include "au8820.h"
#include "au88x0.h"
static struct pci_device_id snd_vortex_ids[] __devinitdata = {
	{PCI_VENDOR_ID_AUREAL, PCI_DEVICE_ID_AUREAL_VORTEX_1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0,},
	{0,}
};

#include "au88x0_synth.c"
#include "au88x0_core.c"
#include "au88x0_pcm.c"
#include "au88x0_mpu401.c"
#include "au88x0_game.c"
#include "au88x0_mixer.c"
#include "au88x0.c"
