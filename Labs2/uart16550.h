#ifndef _UART16550_H
#define _UART16550_H

#define OPTION_COM1                     1
#define OPTION_COM2                     2
#define OPTION_BOTH                     3

#define UART16550_COM1_SELECTED         0x01
#define UART16550_COM2_SELECTED         0x02

#define MAX_NUMBER_DEVICES              2

#define UART16550_IOCTL_SET_LINE        1

struct uart16550_line_info {
        unsigned char baud, len, par, stop;
};


#define COM1_BASEPORT                   0x3f8
#define COM2_BASEPORT                   0x2f8
#define COM1_IRQ                        4
#define COM2_IRQ                        3

#define FIFO_SIZE			4096
#endif /* _UART16550_H_ */
