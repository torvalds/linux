/*
 *  MPC1211 specific keybord definitions
 *  Taken from the old asm-i386/keybord.h for PC/AT-style definitions
 *  created 3 Nov 1996 by Geert Uytterhoeven.
 */

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/kd.h>
#include <linux/pm.h>
#include <asm/io.h>

#define KEYBOARD_IRQ			1
#define DISABLE_KBD_DURING_INTERRUPTS	0

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern int pckbd_pm_resume(struct pm_dev *, pm_request_t, void *);
extern pm_callback pm_kbd_request_override;
extern unsigned char pckbd_sysrq_xlate[128];

#define kbd_setkeycode		pckbd_setkeycode
#define kbd_getkeycode		pckbd_getkeycode
#define kbd_translate		pckbd_translate
#define kbd_unexpected_up	pckbd_unexpected_up
#define kbd_leds		pckbd_leds
#define kbd_init_hw		pckbd_init_hw
#define kbd_sysrq_xlate		pckbd_sysrq_xlate

#define SYSRQ_KEY 0x54

/* resource allocation */
#define kbd_request_region()
#define kbd_request_irq(handler) request_irq(KEYBOARD_IRQ, handler, 0, \
                                             "keyboard", NULL)

/* How to access the keyboard macros on this platform.  */
#define kbd_read_input() inb(KBD_DATA_REG)
#define kbd_read_status() inb(KBD_STATUS_REG)
#define kbd_write_output(val) outb(val, KBD_DATA_REG)
#define kbd_write_command(val) outb(val, KBD_CNTL_REG)

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { } while(0)

/*
 * Machine specific bits for the PS/2 driver
 */

#define AUX_IRQ 12

#define aux_request_irq(hand, dev_id)					\
	request_irq(AUX_IRQ, hand, SA_SHIRQ, "PS2 Mouse", dev_id)

#define aux_free_irq(dev_id) free_irq(AUX_IRQ, dev_id)

#endif /* __KERNEL__ */
