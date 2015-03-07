#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include "uart16550.h"
#include "uart16550_hw.h"

MODULE_DESCRIPTION("Uart16550 driver");
MODULE_LICENSE("GPL");

#ifdef __DEBUG
 #define dprintk(fmt, ...)     printk(KERN_DEBUG "%s:%d " fmt, \
                                __FILE__, __LINE__, ##__VA_ARGS__)
#else
 #define dprintk(fmt, ...)     do { } while (0)
#endif

static struct class *uart16550_class = NULL;
/*
 * TODO: Populate major number from module options (when it is given).
 */
static int major = 42;

static int uart16550_write(struct file *file, const char *user_buffer,
        size_t size, loff_t *offset)
{
        int bytes_copied;
        uint32_t device_port;
        /*
         * TODO: Write the code that takes the data provided by the
         *      user from userspace and stores it in the kernel
         *      device outgoing buffer.
         * TODO: Populate bytes_copied with the number of bytes
         *      that fit in the outgoing buffer.
         */

        uart16550_hw_force_interrupt_reemit(device_port);

        return bytes_copied;
}

irqreturn_t interrupt_handler(int irq_no, void *data)
{
        int device_status;
        uint32_t device_port;
        /*
         * TODO: Write the code that handles a hardware interrupt.
         * TODO: Populate device_port with the port of the correct device.
         */

        device_status = uart16550_hw_get_device_status(device_port);

        while (uart16550_hw_device_can_send(device_status)) {
                uint8_t byte_value;
                /*
                 * TODO: Populate byte_value with the next value
                 *      from the kernel device outgoing buffer.
                 */
                uart16550_hw_write_to_device(device_port, byte_value);
                device_status = uart16550_hw_get_device_status(device_port);
        }

        while (uart16550_hw_device_has_data(device_status)) {
                uint8_t byte_value;
                byte_value = uart16550_hw_read_from_device(device_port);
                /*
                 * TODO: Store the read byte_value in the kernel device
                 *      incoming buffer.
                 */
                device_status = uart16550_hw_get_device_status(device_port);
        }

        return IRQ_HANDLED;
}

static int uart16550_init(void)
{
        int have_com1, have_com2;
        /*
         * TODO: Write driver initialization code here.
         * TODO: have_com1 & have_com2 need to be set according to the
         *      module parameters.
         * TODO: Check return values of functions used. Fail gracefully.
         */

        /*
         * Setup a sysfs class & device to make /dev/com1 & /dev/com2 appear.
         */
        uart16550_class = class_create(THIS_MODULE, "uart16550");

        if (have_com1) {
                /* Setup the hardware device for COM1 */
                uart16550_hw_setup_device(COM1_BASEPORT, THIS_MODULE->name);
                /* Create the sysfs info for /dev/com1 */
                device_create(uart16550_class, NULL, MKDEV(major, 0), NULL, "com1");
        }
        if (have_com2) {
                /* Setup the hardware device for COM2 */
                uart16550_hw_setup_device(COM2_BASEPORT, THIS_MODULE->name);
                /* Create the sysfs info for /dev/com2 */
                device_create(uart16550_class, NULL, MKDEV(major, 1), NULL, "com2");
        }
        return 0;
}

static void uart16550_cleanup(void)
{
        int have_com1, have_com2;
        /*
         * TODO: Write driver cleanup code here.
         * TODO: have_com1 & have_com2 need to be set according to the
         *      module parameters.
         */
        if (have_com1) {
                /* Reset the hardware device for COM1 */
                uart16550_hw_cleanup_device(COM1_BASEPORT);
                /* Remove the sysfs info for /dev/com1 */
                device_destroy(uart16550_class, MKDEV(major, 0));
        }
        if (have_com2) {
                /* Reset the hardware device for COM2 */
                uart16550_hw_cleanup_device(COM2_BASEPORT);
                /* Remove the sysfs info for /dev/com2 */
                device_destroy(uart16550_class, MKDEV(major, 1));
        }

        /*
         * Cleanup the sysfs device class.
         */
        class_unregister(uart16550_class);
        class_destroy(uart16550_class);
}

module_init(uart16550_init)
module_exit(uart16550_cleanup)
