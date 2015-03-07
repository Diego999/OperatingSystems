#ifndef _UART16550_HW
#define _UART16550_HW

#include "uart16550.h"
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/types.h>

#define UART16550_BAUD_1200     96
#define UART16550_BAUD_2400     48
#define UART16550_BAUD_4800     24
#define UART16550_BAUD_9600     12
#define UART16550_BAUD_19200    6
#define UART16550_BAUD_38400    3
#define UART16550_BAUD_56000    2
#define UART16550_BAUD_115200   1

#define UART16550_LEN_5         0x00
#define UART16550_LEN_6         0x01
#define UART16550_LEN_7         0x02
#define UART16550_LEN_8         0x03

#define UART16550_STOP_1        0x00
#define UART16550_STOP_2        0x04

#define UART16550_PAR_NONE      0x00
#define UART16550_PAR_ODD       0x08
#define UART16550_PAR_EVEN      0x18
#define UART16550_PAR_STICK     0x20

/*
 * Extra helper macros.
 */

#define NR_IO_PORTS     8
#define THR             0x00
#define RBR             0x00
#define IER             0x01
#define DLL             0x00
#define DLM             0x01
#define ISR             0x02
#define FCR             0x02
#define LCR             0x03
#define MCR             0x04
#define LSR             0x05
#define MSR             0x06
#define SCR             0x07

#define WRITE_TO_REG(port, reg, value)  outb(value, port + reg)
#define READ_FROM_REG(port, reg)        inb(port + reg)


static inline void uart16550_hw_disable_interrupts(uint32_t port)
{
        WRITE_TO_REG(port, IER, 0x00);
}

static inline void uart16550_hw_enable_interrupts(uint32_t port)
{
        /* Emit interrupt for RDAI and THREI. */
        WRITE_TO_REG(port, IER, 0x03);
}

static inline void uart16550_hw_force_interrupt_reemit(uint32_t port)
{
        uart16550_hw_disable_interrupts(port);
        uart16550_hw_enable_interrupts(port);
}

static inline void uart16550_hw_set_line_parameters(uint32_t port,
                struct uart16550_line_info parameters)
{
        WRITE_TO_REG(port, IER, 0x00); /* Disable the interrupt */
        /* DLAB set to high */
        WRITE_TO_REG(port, LCR, READ_FROM_REG(port, LCR) | 0x80);
        WRITE_TO_REG(port, DLL, parameters.baud); /* Set baud low byte */
        WRITE_TO_REG(port, DLM, 0x00); /* Set baud divisor high byte */
        /* DLAB set to low, length, stop and parity in place. */
        WRITE_TO_REG(port, LCR, parameters.len |
                        parameters.stop | parameters.par);
        /* Use FIFO with 14 byte triggering. */
        WRITE_TO_REG(port, FCR, 0xc7);

        /* Enable interrupt tri-state buffer. */
        WRITE_TO_REG(port, MCR, 0x08);
        /* Emit interrupt for RDAI and THREI. */
        WRITE_TO_REG(port, IER, 0x03);
}

static inline int uart16550_hw_get_device_status(uint32_t port)
{
        int line_status, isr_status;
        isr_status = READ_FROM_REG(port, ISR);
        isr_status >>= 1; isr_status &= 0x07;

        line_status = READ_FROM_REG(port, LSR);

        return line_status;
}

static inline int uart16550_hw_device_can_send(int device_status)
{
        return device_status & 0x20;
}

static inline int uart16550_hw_device_has_data(int device_status)
{
        return device_status & 0x01;
}

static inline void uart16550_hw_write_to_device(uint32_t port, uint8_t byte)
{
        WRITE_TO_REG(port, THR, byte);
}

static uint8_t uart16550_hw_read_from_device(uint32_t port)
{
        return READ_FROM_REG(port, RBR);
}

static inline int uart16550_hw_setup_device(uint32_t port, char *module_name)
{
        struct uart16550_line_info default_param = {
                UART16550_BAUD_115200,
                UART16550_LEN_8,
                UART16550_STOP_1,
                UART16550_PAR_NONE
        };
        /*
         * Request I/O port access.
         */
        if (!request_region(port, NR_IO_PORTS, module_name))
                return -ENODEV;

        if (READ_FROM_REG(port, LSR) & 0x01)
                READ_FROM_REG(port, RBR);
        READ_FROM_REG(port, ISR);
        READ_FROM_REG(port, MSR);
        uart16550_hw_disable_interrupts(port);
        uart16550_hw_set_line_parameters(port, default_param);

        return 0;
}

static inline void uart16550_hw_cleanup_device(uint32_t port)
{
        /*
         * Release I/O port.
         */
        release_region(port, NR_IO_PORTS);
        /*
         * Disable hardware interrupts.
         */
        WRITE_TO_REG(port, FCR, 0x00);
        uart16550_hw_disable_interrupts(port);
}
#endif /* _UART16550_HW */
