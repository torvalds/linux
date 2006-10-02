extern unsigned char ec3104_kbd_sysrq_xlate[];
extern int ec3104_kbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int ec3104_kbd_getkeycode(unsigned int scancode);
extern int ec3104_kbd_translate(unsigned char, unsigned char *, char);
extern char ec3104_kbd_unexpected_up(unsigned char);
extern void ec3104_kbd_leds(unsigned char);
extern void ec3104_kbd_init_hw(void);

#define kbd_sysrq_xlate ec3104_kbd_sysrq_xlate
#define kbd_setkeycode ec3104_kbd_setkeycode
#define kbd_getkeycode ec3104_kbd_getkeycode
#define kbd_translate ec3104_kbd_translate
#define kbd_unexpected_up ec3104_kbd_unexpected_up
#define kbd_leds ec3104_kbd_leds
#define kbd_init_hw ec3104_kbd_init_hw
