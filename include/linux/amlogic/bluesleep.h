
#ifndef _BLUESLEEP_H
#define _BLUESLEEP_H

#define TIOCSETBTPORT 0x5489
#define TIOCCLRBTPORT 0x5490

struct bt_wake_ops {
    int (*setup_bt_port)(struct uart_port *);
    int (*bt_can_sleep)(void);
};

extern void register_bt_wake_ops(struct bt_wake_ops *);
extern void unregister_bt_wake_ops(void);
#endif

