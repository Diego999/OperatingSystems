#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/moduleparam.h>
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
static struct device *uart16550_device[] = {NULL, NULL};
//static struct mutex uart16550_mutexes[MAX_NUMBER_DEVICES]; Doesn't work, should fine a proper way
static DEFINE_MUTEX(com1_mutex);
static DEFINE_MUTEX(com2_mutex);
/*
 * TODO: Populate major number from module options (when it is given).
 */
static int major = DEFAULT_MAJOR;
static int behavior = OPTION_BOTH;

static int uart16550_open(struct inode *inode, struct file *filp)
{
	int minor = iminor(inode);
	struct device *current_device = uart16550_device[minor];
	//Not used now, but might be useful later
	filp->private_data = NULL; 

	if((minor == MINOR_COM1 && !mutex_trylock(&com1_mutex)) || (minor == MINOR_COM2 && !mutex_trylock(&com2_mutex)))
		return -EBUSY;

	//Should we test whether the driver is READ & WRITE ?
	if((filp->f_flag & O_ACCMODE) != O_RDWR))
		return -EACCES;
	
	return 0;
}

static int uart16550_close(struct inode* inode, struct file* filp)
{
	//Free filp->private_data if necessary
	if(iminor(inode) == MINOR_COM1)
		mutex_unlock(&com1_mutex);
	else if(iminor(inode) == MINOR_COM2)
		mutex_unlock(&com2_mutex);

	return 0;
}

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

static void init_have_com_x(int* have_com1, int* have_com2)
{
		//Assign values corresponding to behavior or set to the default value (OPTION_BOTH)
		*have_com1 = (behavior == OPTION_COM2) ? 0 : 1;
		*have_com2 = (behavior == OPTION_COM1) ? 0 : 1;
}

static void init_mutexes(void)
{
	int i;
	for(i = 0; i < MAX_NUMBER_DEVICES; ++i)
	{
		/*uart16550_mutexes[i] = */__MUTEX_INITIALIZER(uart16550_mutexes[i]);
	}
}

static int uart16550_init(void)
{
        int have_com1, have_com2;
        int minor;
        /*
         * TODO: Write driver initialization code here.
         * TODO: have_com1 & have_com2 need to be set according to the
         *      module parameters.
         * TODO: Check return values of functions used. Fail gracefully.
         */

        init_have_com_x(&have_com1, &have_com2);



        if(major <= 0)
        	major = DEFAULT_MAJOR;
        /*
         * Setup a sysfs class & device to make /dev/com1 & /dev/com2 appear.
         */
        uart16550_class = class_create(THIS_MODULE, "uart16550");
        if(IS_ERR(uart16550_class))
        	return PTR_ERR(uart16550_class);

        if (have_com1) {
        		minor = MINOR_COM1;
        		mutex_init(&com1_mutex);

                /* Setup the hardware device for COM1 */
                uart16550_hw_setup_device(COM1_BASEPORT, THIS_MODULE->name);
                /* Create the sysfs info for /dev/com1 */
                uart16550_device[minor] = device_create(uart16550_class, NULL, MKDEV(major, minor), NULL, "com1");
                if(IS_ERR(uart16550_device[minor]))
                	return PTR_ERR(uart16550_device[minor]);
        }
        if (have_com2) {
        		minor = MINOR_COM2;
                mutex_init(&com2_mutex);

                /* Setup the hardware device for COM2 */
                uart16550_hw_setup_device(COM2_BASEPORT, THIS_MODULE->name);
                /* Create the sysfs info for /dev/com2 */
                uart16550_device[minor] = device_create(uart16550_class, NULL, MKDEV(major, minor), NULL, "com2");
                if(IS_ERR(uart16550_device[minor]))
                	return PTR_ERR(uart16550_device[minor]);
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

		init_have_com_x(&have_com1, &have_com2);

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

module_param(major, int, S_IRUGO);
MODULE_PARM_DESC(major, "The major number of the character device which will be used to interact with the driver. Default value : 42.");
module_param(behavior, int, S_IRUGO);
MODULE_PARM_DESC(behavior, "Wheter the driver should connect to COM1, COM2 or both. Default value : BOTH. Values are 0x01, 0x02 or 0x03.");
module_init(uart16550_init)
module_exit(uart16550_cleanup)
