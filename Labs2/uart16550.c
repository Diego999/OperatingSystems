/*
 * EPFL - Operating systems
 * Assignment 2 : Serial port driver
 * Diego Antognini & Jason Racine
 *
 * Best reading setting for this file :
 *    indentation = 1 tab, corresponds to 4 spaces
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include "uart16550.h"
#include "uart16550_hw.h"

MODULE_DESCRIPTION("Uart16550 driver");
MODULE_LICENSE("GPL");

// *****************************************************************************
// **                 DEBUGGING MACRO, DEACTIVATE IN Kbuild                   **
// *****************************************************************************
#ifdef __DEBUG
#define dprintk(fmt, ...)     printk(KERN_DEBUG "%s:%d " fmt, \
	__FILE__, __LINE__, ##__VA_ARGS__)
#else
#define dprintk(fmt, ...)     do { } while (0)
#endif

// *****************************************************************************
// **            STRUCT REPRESENTING A CONCURRENT-PROTECTED BUFFER            **
// *****************************************************************************
struct uart16550_buffer {
	DECLARE_KFIFO(buffer, uint8_t, FIFO_SIZE);	// FIFO buffer
	spinlock_t mutex_lock;						// lock for concurrent accesses
	struct semaphore wait_cond;					// semaphore for passive wait
	int need_notify_flag;						// flag for notification on wait
};

// *****************************************************************************
// **                    STRUCT REPRESENTING A COM DEVICE                     **
// *****************************************************************************
struct uart16550_device {

	// COM port ID (UART16550_COM[1|2]_SELECTED)
	unsigned int com_id;

	// COM port physical address (COM[1|2]_BASEPORT)
	uint32_t com_port;

	// Char device struct held when device files are active in /dev
	struct cdev chrdev;

	// Input buffer, for data incoming from device
	struct uart16550_buffer buffer_in;

	// Output buffer, for data outgoing to device
	struct uart16550_buffer buffer_out;

	// Busy variable protected by a lock, to prevent multiple process from
	// opening the same device twice
	spinlock_t busy_lock;
	int busy;
};

// *****************************************************************************
// **                               GLOBALS                                   **
// *****************************************************************************

// class structure for managing the char device
static struct class* uart16550_class = NULL;

// array storing the managed devices
struct uart16550_device devices[MAX_NUMBER_DEVICES];

// *****************************************************************************
// **                 MODULE PARAMETERS DEFINITION AND EXPORT                 **
// *****************************************************************************

// major parameter : allows to define explicitely the major device number to use
static int major = 42;		// default value 42 imposed by assignment
module_param(major, int, S_IRUGO);

// behavior parameter : allows to specify whether this driver must manage COM1,
// COM2 or both at the same time
static int behavior = OPTION_BOTH;		// default value imposed by assignment
module_param(behavior, int, S_IRUGO);

// *****************************************************************************
// **                          FUNCTIONS PROTOTYPES                           **
// *****************************************************************************

// module init function
static int uart16550_mod_init(void);
static void uart16550_mod_cleanup(void);

// file operations
static int uart16550_fop_open(struct inode* inode, struct file* file);
static int uart16550_fop_release(struct inode* inode, struct file* file);
static ssize_t uart16550_fop_read(struct file* file, char* user_buffer, size_t size,
	loff_t* offset);
static ssize_t uart16550_fop_write(struct file* file, const char* user_buffer,
	size_t size, loff_t* offset);
static long uart16550_fop_ioctl(struct file* file, unsigned int op,
	unsigned long arg);
static long uart16550_fop_ioctl_set_line(struct file* file, unsigned long arg);

// interrupt handler
irqreturn_t uart16550_interrupt_handler(int irq_no, void *data);

// device management
static int uart16550_device_init(struct uart16550_device* device,
	uint32_t com_port, unsigned int com_id, int irq_no, const char* name);
static void uart16550_device_cleanup(struct uart16550_device* device);
static int uart16550_device_open(struct uart16550_device* device,
	struct file* file);
static int uart16550_device_release(struct uart16550_device* device);
static int uart16550_device_read(struct uart16550_device* device, char* buffer,
	size_t size, int* copied);
static int uart16550_device_write(struct uart16550_device* device, char* buffer,
	size_t size, int* copied);
static int uart16550_device_ioctl_set_line(struct uart16550_device* device,
	struct uart16550_line_info info);

// buffer management
static void uart16550_buffer_init(struct uart16550_buffer* buffer);

// *****************************************************************************
// **            FILE OPERATIONS ASSOCIATION THROUGH FOPS STRUCT              **
// *****************************************************************************
struct file_operations fops = {
	.owner				= THIS_MODULE,
	.open				= uart16550_fop_open,
	.release			= uart16550_fop_release,
	.read				= uart16550_fop_read,
	.write				= uart16550_fop_write,
	.unlocked_ioctl		= uart16550_fop_ioctl,
};

// *****************************************************************************
// **                         MODULE INIT FUNCTION                            **
// *****************************************************************************

static int uart16550_mod_init(void)
{
	// Local variables
	int have_com1;		// bool value : has COM1 to be managed by our driver ?
	int have_com2;		// bool value : has COM2 to be managed by our driver ?
	int err_code;		// error code issued by subcalls

	// Debug : print module parameters settings
	dprintk("uart16550_mod_init\n");
	dprintk("  -> major = %d\n", major);
	dprintk("  -> behavior = %d\n", behavior);

	// Correct major number
	// major is assumed on 12 bits as said in doc, 2^12=4096 numbers representable
	if (major<0 || major > 4095)
	{
		return -EINVAL;
	}

	// Set have_com1 and have_com2 and create char device region in memory
	switch (behavior)
	{
		case OPTION_COM1:
			have_com1 = 1;
			have_com2 = 0;
			if ((err_code = register_chrdev_region(MKDEV(major,0), 1, "uart16550"))
				!= 0)
			{
				unregister_chrdev_region(MKDEV(major,0), 1);
			}
			break;

		case OPTION_COM2:
			have_com1 = 0;
			have_com2 = 1;
			if ((err_code = register_chrdev_region(MKDEV(major,1), 1, "uart16550"))
				!= 0)
			{
				unregister_chrdev_region(MKDEV(major,1), 1);
			}
			break;

		case OPTION_BOTH:
			have_com1 = 1;
			have_com2 = 1;
			if ((err_code = register_chrdev_region(MKDEV(major,0), 2, "uart16550"))
				!= 0)
			{
				unregister_chrdev_region(MKDEV(major,0), 2);
			}
			break;

		default:
			return -ENODEV;
	}

	// Check for error while registering char device region
	if (err_code == 0)
	{
		dprintk("  -> regions registered sucessfully\n");
	}
	else
	{
		dprintk("  -> unable to register regions\n");
		return err_code;
	}

	// Setup sysfs class
	uart16550_class = class_create(THIS_MODULE, "uart16550");

	// Enable COM1 if needed
	if (have_com1)
	{
		// Initialize device
		switch (uart16550_device_init(&(devices[0]), COM1_BASEPORT,
			UART16550_COM1_SELECTED, COM1_IRQ, "com1"))
		{
			case 0:
				break;
			case -ERR_DEV_INIT_INTERRUPT:
				return -EBUSY;
			case -ERR_DEV_INIT_LIVE:
				return -ENOENT;
			default:
				return -EAGAIN;
		}
	}

	// Enable COM2 if needed
	if (have_com2)
	{
		// Initialize device
		switch (uart16550_device_init(&(devices[1]), COM2_BASEPORT,
			UART16550_COM2_SELECTED, COM2_IRQ, "com2"))
		{
			case 0:
				break;
			case -ERR_DEV_INIT_INTERRUPT:
				return -EBUSY;
			case -ERR_DEV_INIT_LIVE:
				return -ENOENT;
			default:
				return -EAGAIN;
		}
	}

	return 0;
}

// *****************************************************************************
// **                       MODULE CLEANUP FUNCTION                           **
// *****************************************************************************

static void uart16550_mod_cleanup(void)
{
	// Local variables
	int have_com1;		// bool value : has COM1 to be managed by our driver ?
	int have_com2;		// bool value : has COM2 to be managed by our driver ?
	int minor;			// minor device number
	int count;			// number of devices registered

	// Debug : print function call
	dprintk("uart16550_mod_cleanup\n");

	// Set have_com1 and have_com2 according to the behavior
	switch (behavior)
	{
		case OPTION_COM1:
			have_com1 = 1;
			have_com2 = 0;
			minor = 0;
			count = 1;
			break;

		case OPTION_COM2:
			have_com1 = 0;
			have_com2 = 1;
			minor = 1;
			count = 1;
			break;

		case OPTION_BOTH:
			have_com1 = 1;
			have_com2 = 1;
			minor = 0;
			count = 2;
			break;

		default:
			return;
	}

	// Clear COM1 if needed
	if (have_com1)
	{
		uart16550_device_cleanup(&(devices[0]));
	}

	// Clear COM2 if needed
	if (have_com2)
	{
		uart16550_device_cleanup(&(devices[1]));
	}

	// Cleanup device class
	class_unregister(uart16550_class);
	class_destroy(uart16550_class);

	// Unregister char device region
	unregister_chrdev_region(MKDEV(major,minor),count);

	return;
}

// *****************************************************************************
// **                         FILE OPERATION : OPEN                           **
// *****************************************************************************

static int uart16550_fop_open(struct inode* inode, struct file* file)
{
	// Local variables
	int err_code;		// Error code from subcalls

	// Debug message
	dprintk("uart16550_fop_open\n");

	// Find which device is used, and open it
	switch (iminor(inode))
	{
		case 0:
			dprintk("  -> opening COM1\n");
			err_code = uart16550_device_open(&(devices[0]), file);
			break;

		case 1:
			dprintk("  -> opening COM2\n");
			err_code = uart16550_device_open(&(devices[1]), file);
			break;

		default:
			dprintk("  -> FAILED to open unexisting device\n");
			return -ENODEV;
	}

	// Check for error
	switch (err_code)
	{
		case 0:
			return 0;
		case -ERR_DEV_OPEN_ALREADY_USED:
			return -EBUSY;
		default:
			return -EAGAIN;
	}
}

// *****************************************************************************
// **                        FILE OPERATION : RELEASE                         **
// *****************************************************************************

static int uart16550_fop_release(struct inode* inode, struct file* file)
{
	// Local variables
	int err_code;						// Error code from subcalls

	// Debug message
	dprintk("uart16550_fop_release\n");

	// Find which device is used, and release it
	switch (iminor(inode))
	{
		case 0:
			dprintk("  -> releasing COM1\n");
			err_code = uart16550_device_release(&(devices[0]));
			break;

		case 1:
			dprintk("  -> releasing COM2\n");
			err_code = uart16550_device_release(&(devices[1]));
			break;

		default:
			dprintk("  -> FAILED to close unexisting device\n");
			return -ENODEV;
	}

	// Check for error
	switch (err_code)
	{
		case 0:
			return 0;
		case -ERR_DEV_RELEASE_NOT_OPEN:
			return -EBADFD;
		default:
			return -EAGAIN;
	}
}

// *****************************************************************************
// **                         FILE OPERATION : READ                           **
// *****************************************************************************

static ssize_t uart16550_fop_read(struct file* file, char* user_buffer,
	size_t size, loff_t* offset)
{
	// Local variables
	struct uart16550_device* device;	// device to read from
	int bytes_copied;		// number of bytes copied by device read procedure
	int err_code;			// error code from subcalls
	char* kernel_buffer;	// kernel buffer for retrieving data

	// Debug message
	dprintk("uart16550_fop_read\n");

	// Grab device from file structure
	device = file->private_data;

	// Writing info
	dprintk("  -> reading from COM%d\n", device->com_id);

	// Allocate kernel buffer
	kernel_buffer = (char*)kmalloc(sizeof(char)*size, GFP_KERNEL);
	if (!kernel_buffer)
	{
		return -ENOMEM;
	}

	// Retrieve data from device buffer
	// return value is ignored here, because some bytes (may 0) could have been
	// read, these bytes will be returned with correct number of bytes as return
	// value
	bytes_copied = 0;
	switch (uart16550_device_read(device, kernel_buffer, size, &bytes_copied))
	{
		case 0:
			dprintk("  -> successfully retrieved %d bytes from device buffer\n",
				bytes_copied);
			break;
		case -ERR_DEV_READ_BUFFER_ERROR:
			dprintk("  -> only %d bytes retrieved, error in KFIFO\n",
				bytes_copied);
			break;
		case -ERR_DEV_READ_UNKNOWN:
			dprintk("  -> only %d bytes retrieved, unknown error\n",
				bytes_copied);
			break;
		default:
			dprintk("  -> only %d bytes retrieved, VERY unknown error\n",
				bytes_copied);
			break;
	}

	// Copy buffer to user_space
	err_code = copy_to_user(user_buffer, kernel_buffer,
		sizeof(char)*bytes_copied);

	// Free kernel buffer
	kfree(kernel_buffer);

	// Deal with error while copying to user space
	switch (err_code)
	{
		case 0:
			dprintk("  -> %d bytes copied to user space, everything went fine\n",
				bytes_copied);
			return bytes_copied;
		default:
			dprintk("  -> %d bytes copied to user space, ERROR !\n",
				bytes_copied - err_code);
			return -EFAULT;
	}
}

// *****************************************************************************
// **                         FILE OPERATION : WRITE                          **
// *****************************************************************************

static ssize_t uart16550_fop_write(struct file* file, const char* user_buffer,
	size_t size, loff_t* offset)
{
	// Local variables
	struct uart16550_device* device;	// device to write to
	int bytes_copied;		// number of bytes copied by device write procedure
	int err_code;			// error code from subcalls
	char* kernel_buffer;	// kernel buffer for retrieving data

	// Debug message
	dprintk("uart16550_fop_write\n");

	// Grab device from file structure
	device = file->private_data;

	// Writing info
	dprintk("  -> writing to COM%d\n", device->com_id);

	// Allocate kernel buffer
	kernel_buffer = (char*)kmalloc(sizeof(char)*size, GFP_KERNEL);
	if (!kernel_buffer)
	{
		return -ENOMEM;
	}

	// Fill kernel buffer with user data
	err_code = copy_from_user(kernel_buffer, user_buffer, sizeof(char)*size);

	// Deal with error while gathering from user space
	switch (err_code)
	{
		case 0:
			dprintk("  -> %d bytes copied from user space, everything went fine\n",
				size);
			break;
		default:
			dprintk("  -> %d bytes copied from user space, missing %d, ERROR !\n",
				size-err_code, err_code);
			kfree(kernel_buffer);
			return -EFAULT;
	}

	// Send data to device
	bytes_copied = 0;
	switch (uart16550_device_write(device, kernel_buffer, size, &bytes_copied))
	{
		case 0:
			dprintk("  -> successfully written %d bytes to device buffer\n",
				bytes_copied);
			break;
		case -ERR_DEV_WRITE_BUFFER_ERROR:
			dprintk("  -> only %d bytes written, error in KFIFO\n",
				bytes_copied);
			break;
		case -ERR_DEV_WRITE_UNKNOWN:
			dprintk("  -> only %d bytes written, unknown error\n",
				bytes_copied);
			break;
		default:
			dprintk("  -> only %d bytes written, VERY unknown error\n",
				bytes_copied);
			break;
	}

	// Free kernel buffer
	kfree(kernel_buffer);

	// return number of bytes copied
	return bytes_copied;
}

// *****************************************************************************
// **                         FILE OPERATION : IOCTL                          **
// *****************************************************************************

static long uart16550_fop_ioctl(struct file* file, unsigned int op,
	unsigned long arg)
{
	// Dispatch operations
	switch (op)
	{
		case UART16550_IOCTL_SET_LINE:
			return uart16550_fop_ioctl_set_line(file, arg);
		default:
			return -EPERM;
	}
}

static long uart16550_fop_ioctl_set_line(struct file* file, unsigned long arg)
{
	// Local variables
	struct uart16550_device* device;	// device to set infos
	struct uart16550_line_info info;	// info to set on device, kernel-space
	int err_code;						// error code from subcalls

	// Grab device from file structure
	device = file->private_data;

	// Retrieve line info from user space
	err_code = copy_from_user(&info, (struct uart16550_line_info*)arg, 
		sizeof(struct uart16550_line_info));

	// Deal with error code
	switch (err_code)
	{
		// everything was OK while copying from user space
		case 0:
			break;

		// error occurred while copying from user space
		default:
			return -EFAULT;
	}

	// Call device procedure
	err_code = uart16550_device_ioctl_set_line(device, info);

	// Deal with error code
	switch (err_code)
	{
		case 0:
			return 0;
		default:
			return -EAGAIN;
	}
}

// *****************************************************************************
// **                           INTERRUPT HANDLER                             **
// *****************************************************************************

irqreturn_t uart16550_interrupt_handler(int irq_no, void *data)
{
	// Local variables
	struct uart16550_device* device;
	int device_status;			// status of HW device
	uint8_t byte;				// byte to read/write
	int space_available;		// space available in input buffer (to read)
	int space_used;				// space used in output buffer (to write)
	int nb_read_bytes;			// number of bytes read from device
	int nb_write_bytes;			// number of bytes written to device
	unsigned long irq_flags;	// IRQ flags backup during spinlocks
	int byte_treated;			// number of bytes treated by FIFO
	int err_code;				// internal error code

	// Grab device from interrupt data
	device = data;

	// Debug message
	dprintk("### uart16550_interrupt_handler ###\n");
	dprintk("  ++ from COM%d\n", device->com_id);

	// Write to device
	{
		// Initialize byte counter, buffer status and device status
		nb_write_bytes = 0;
		device_status = uart16550_hw_get_device_status(device->com_port);
		err_code = 0;
		spin_lock_irqsave(&(device->buffer_out.mutex_lock), irq_flags);
		space_used = kfifo_len(&(device->buffer_out.buffer));
		spin_unlock_irqrestore(&(device->buffer_out.mutex_lock), irq_flags);

		// Send bytes to device while possible
		while (uart16550_hw_device_can_send(device_status) && (space_used>0)
			&& (err_code==0))
		{
			// Get data from buffer
			spin_lock_irqsave(&(device->buffer_out.mutex_lock), irq_flags);
			byte_treated = kfifo_get(&(device->buffer_out.buffer), &byte);
			spin_unlock_irqrestore(&(device->buffer_out.mutex_lock), irq_flags);

			// Deal with error
			switch (byte_treated)
			{
				// everything OK, 1 byte retrieved from fifo
				case 1:
					break;

				// something went wrong, stop writing procedure
				default:
					err_code = 1;
					break;
			}

			// If data succesfully retrieved, write to device and continue loop
			if (!err_code)
			{
				uart16550_hw_write_to_device(device->com_port, byte);
				nb_write_bytes++;
				device_status = uart16550_hw_get_device_status(device->com_port);
				spin_lock_irqsave(&(device->buffer_out.mutex_lock), irq_flags);
				space_used = kfifo_len(&(device->buffer_out.buffer));
				spin_unlock_irqrestore(&(device->buffer_out.mutex_lock),
					irq_flags);
			}
		}

		// Send semaphore liberation to write() if it was waiting on buffer to
		// free space
		if (nb_write_bytes > 0)
		{
			spin_lock_irqsave(&(device->buffer_out.mutex_lock), irq_flags);
			if (device->buffer_out.need_notify_flag > 0)
			{
				up(&(device->buffer_out.wait_cond));
			}
			spin_unlock_irqrestore(&(device->buffer_out.mutex_lock), irq_flags);
		}
	}

	// Read from device
	{
		// Initialize byte counter, buffer status and device status
		nb_read_bytes = 0;
		device_status = uart16550_hw_get_device_status(device->com_port);
		err_code = 0;
		spin_lock_irqsave(&(device->buffer_in.mutex_lock), irq_flags);
		space_available = kfifo_avail(&(device->buffer_in.buffer));
		spin_unlock_irqrestore(&(device->buffer_in.mutex_lock), irq_flags);

		// Read bytes from device while possible
		while (uart16550_hw_device_has_data(device_status)
			&& (space_available>0) && (err_code==0))
		{
			// Get data from device
			byte = uart16550_hw_read_from_device(device->com_port);

			// Put data in buffer
			spin_lock_irqsave(&(device->buffer_in.mutex_lock), irq_flags);
			byte_treated = kfifo_put(&(device->buffer_in.buffer), byte);
			spin_unlock_irqrestore(&(device->buffer_in.mutex_lock), irq_flags);

			// Deal with error
			switch (byte_treated)
			{
				// everything OK, 1 byte written to fifo
				case 1:
					break;

				// something went wrong, stop reading procedure
				default:
					err_code = 1;
					break;
			}

			// If data successfully written, continue loop
			if (!err_code)
			{
				nb_read_bytes++;
				device_status = uart16550_hw_get_device_status(device->com_port);
				spin_lock_irqsave(&(device->buffer_in.mutex_lock), irq_flags);
				space_available = kfifo_avail(&(device->buffer_in.buffer));
				spin_unlock_irqrestore(&(device->buffer_in.mutex_lock),
					irq_flags);
			}
		}

		// Send semaphore liberation to read() if it was waiting on buffer to
		// add data
		if (nb_read_bytes > 0)
		{
			spin_lock_irqsave(&(device->buffer_in.mutex_lock), irq_flags);
			if (device->buffer_in.need_notify_flag > 0)
			{
				up(&(device->buffer_in.wait_cond));
			}
			spin_unlock_irqrestore(&(device->buffer_in.mutex_lock), irq_flags);
		}
	}

	// End of IRQ
	return IRQ_HANDLED;
}

// *****************************************************************************
// **                          DEVICES MANAGEMENT                             **
// *****************************************************************************

static int uart16550_device_init(struct uart16550_device* device,
	uint32_t com_port, unsigned int com_id, int irq_no, const char* name)
{
	// Local variables
	dev_t curr_dev;		// device number container

	// Setup the hardware device
	uart16550_hw_setup_device(com_port, THIS_MODULE->name);
	device->com_port = com_port;

	// Generate device number
	curr_dev = MKDEV(major, com_id-1);
	device->com_id = com_id;

	// Prepare the char device
	cdev_init(&(device->chrdev), &fops);
	device->chrdev.ops = &fops;
	device->chrdev.owner = THIS_MODULE;

	// Initialize global device spinlock
	spin_lock_init(&(device->busy_lock));
	device->busy = 0;

	// Initialize buffers
	uart16550_buffer_init(&(device->buffer_in));
	uart16550_buffer_init(&(device->buffer_out));

	// Request for corresponding interrupts
	if (request_irq(irq_no, uart16550_interrupt_handler, IRQF_SHARED, "uart16550",
		device) != 0)
	{
		dprintk("  -> request for interrupts on IRQ%d (COM%d) FAILED\n",
			irq_no, com_id);
		return -ERR_DEV_INIT_INTERRUPT;
	}

	dprintk("  -> request for interrupts on IRQ%d (COM%d) OK\n", irq_no,
		com_id);

	// Put device live
	if (cdev_add(&(device->chrdev), curr_dev, 1) != 0)
	{
		dprintk("  -> registration of COM%d FAILED\n", com_id);
		return -ERR_DEV_INIT_LIVE;
	}

	dprintk("  -> registration of COM%d OK\n", com_id);

	// Create the sysfs info for /dev/comX
	device_create(uart16550_class, NULL, curr_dev, NULL, name);

	return 0;
}

static void uart16550_device_cleanup(struct uart16550_device* device)
{
	// Local variables
	dev_t curr_dev;		// device number container
	int irq_no;			// IRQ number

	// Find IRQ number
	switch (device->com_id)
	{
		case UART16550_COM1_SELECTED:
			irq_no = COM1_IRQ;
			break;
		case UART16550_COM2_SELECTED:
			irq_no = COM2_IRQ;
			break;
		default:
			return;
	}

	// Generate device number
	curr_dev = MKDEV(major, device->com_id-1);

	// Remove the sysfs entry for /dev/comX
	device_destroy(uart16550_class, curr_dev);

	// Stop responding to IRQs
	free_irq(irq_no, device);

	// Unregister the device
	cdev_del(&(device->chrdev));

	// Reset the hardware device
	uart16550_hw_cleanup_device(device->com_port);

	return;
}

static int uart16550_device_open(struct uart16550_device* device,
	struct file* file)
{
	// Local variables
	unsigned long irq_flags;	// IRQ flags for spinlocks
	int busy;					// busy flag for corresponding device

	// Live section
	spin_lock_irqsave(&(device->busy_lock), irq_flags);
	{
		// Gather previous busy state
		//    FALSE = device succesfully acquired
		//    TRUE = device already occupied
		busy = device->busy;

		// If device available, take it
		if (!busy)
		{
			device->busy = 1;
		}
	}
	spin_unlock_irqrestore(&(device->busy_lock), irq_flags);

	// If device succesfully acquired
	if (!busy)
	{
		// Associate device struture pointer to file pointer
		file->private_data = device;

		// Enable hardware interrupts
        uart16550_hw_force_interrupt_reemit(device->com_port);

        // Return success
        return 0;
	}

	// If device already used
	else
	{
		// Return error
		return -ERR_DEV_OPEN_ALREADY_USED;
	}
}

static int uart16550_device_release(struct uart16550_device* device)
{
	// Local variables
	unsigned long irq_flags;		// IRQ flags for spinlocks
	int busy;						// busy flag for corresponding device

	// Live section
	spin_lock_irqsave(&(device->busy_lock), irq_flags);
	{
		// Gather previous busy state
		//    FALSE = device is not used, impossible to release
		//    TRUE = device succesfully released
		busy = device->busy;

		// If device used, release it
		if (busy)
		{
			device->busy = 0;
		}
	}
	spin_unlock_irqrestore(&(device->busy_lock), irq_flags);

	// If device succesfully released
	if (busy)
	{
		// Disable hardware interrupts
		uart16550_hw_disable_interrupts(device->com_port);

		// Return success
		return 0;
	}

	// If device not used
	else
	{
		// Return error
		return -ERR_DEV_RELEASE_NOT_OPEN;
	}
}

static int uart16550_device_read(struct uart16550_device* device, char* buffer,
	size_t size, int* copied)
{
	// Local variables
	unsigned long irq_flags;		// IRQ flags for spinlocks
	int space_used;					// space used in input buffer
	int bytes_to_copy;				// number of bytes to copy into buffer
	int bytes_copied;				// number of bytes copied into buffer
	int byte_retrieved;				// single byte retrieved or not from buffer?
	int err_code;					// error code from subcalls
	char byte;						// byte read

	// Live section
	spin_lock_irqsave(&(device->buffer_in.mutex_lock), irq_flags);
	{
		// Grab space used in input buffer
		space_used = kfifo_len(&(device->buffer_in.buffer));

		// Wait on notification while no data available
		while (space_used <= 0)
		{
			// Set notification flag
			device->buffer_in.need_notify_flag = 1;

			// Out of live section, go to sleep, return 0 bytes read if
			// interrupted
			spin_unlock_irqrestore(&(device->buffer_in.mutex_lock), irq_flags);
			if (down_interruptible(&(device->buffer_in.wait_cond)) != 0)
			{
				return 0;
			}
			spin_lock_irqsave(&(device->buffer_in.mutex_lock), irq_flags);

			// Clear notification flag
			device->buffer_in.need_notify_flag = 0;

			// Grab new size
			space_used = kfifo_len(&(device->buffer_in.buffer));
		}

		// Determine number of bytes to copy
		if (size <= space_used)
		{
			bytes_to_copy = size;
		}
		else
		{
			bytes_to_copy = space_used;
		}

		// Copy to kernel buffer
		err_code = 0;
		for (bytes_copied = 0; (bytes_copied<bytes_to_copy) && (err_code==0);
			bytes_copied++)
		{
			// Read byte from buffer
			byte_retrieved = kfifo_get(&(device->buffer_in.buffer),
				(uint8_t*)&byte);

			// Act on number of elements retrieved (should be 1 !)
			switch (byte_retrieved)
			{
				// byte succesfully retrieved : put in kernel buffer
				case 1:
					buffer[bytes_copied] = byte;
					break;

				// error : exit loop with error code
				default:
					err_code = 1;
					break;
			}
		}
	}
	spin_unlock_irqrestore(&(device->buffer_in.mutex_lock), irq_flags);

	// Call hardware interrupt to continue filling buffer with available data
	uart16550_hw_force_interrupt_reemit(device->com_port);

	// Put number of bytes read
	*copied = bytes_copied;

	// Deal with error code
	switch (err_code)
	{
		// everything's OK
		case 0:
			return 0;

		// error during retrieving from kfifo
		case 1:
			return -ERR_DEV_READ_BUFFER_ERROR;

		// unknown error
		default:
			return -ERR_DEV_READ_UNKNOWN;
	}
}

static int uart16550_device_write(struct uart16550_device* device, char* buffer,
	size_t size, int* copied)
{
	// Local variables
	unsigned long irq_flags;		// IRQ flags for spinlocks
	int space_available;			// space available in output buffer
	int bytes_to_copy;				// number of bytes to copy into buffer
	int bytes_copied;				// number of bytes copied into buffer
	int byte_written;				// single byte written or not to buffer?
	int err_code;					// error code from subcalls
	char byte;						// byte to write

	// Live section
	spin_lock_irqsave(&(device->buffer_out.mutex_lock), irq_flags);
	{
		// Grab space available in output buffer
		space_available = kfifo_avail(&(device->buffer_out.buffer));

		// Wait on notification while no space available to write
		while (space_available <= 0)
		{
			// Set notification flag
			device->buffer_out.need_notify_flag = 1;

			// Out of live section, go to sleep, return 0 bytes written if
			// interrupted
			spin_unlock_irqrestore(&(device->buffer_out.mutex_lock), irq_flags);
			if (down_interruptible(&(device->buffer_out.wait_cond)) != 0)
			{
				return 0;
			}
			spin_lock_irqsave(&(device->buffer_out.mutex_lock), irq_flags);

			// Clear notification flag
			device->buffer_out.need_notify_flag = 0;

			// Grab new size
			space_available = kfifo_avail(&(device->buffer_out.buffer));
		}

		// Determine number of bytes to write
		if (size <= space_available)
		{
			bytes_to_copy = size;
		}
		else
		{
			bytes_to_copy = space_available;
		}

		// Copy from kernel buffer
		err_code = 0;
		for (bytes_copied = 0; (bytes_copied<bytes_to_copy) && (err_code==0);
			bytes_copied++)
		{
			// Get byte from kernel buffer
			byte = buffer[bytes_copied];

			// Write byte to buffer
			byte_written = kfifo_put(&(device->buffer_out.buffer),
				(uint8_t)byte);

			// Act on number of elements written (should be 1 !)
			switch (byte_written)
			{
				// byte succesfully written : do nothing
				case 1:
					break;

				// error : exit loop with error code
				default:
					err_code = 1;
					break;
			}
		}



	}
	spin_unlock_irqrestore(&(device->buffer_out.mutex_lock), irq_flags);

	// Call hardware interrupt to write new data to hardware device buffer
	uart16550_hw_force_interrupt_reemit(device->com_port);

	// Put number of bytes read
	*copied = bytes_copied;

	// Deal with error code
	switch (err_code)
	{
		// everything's OK
		case 0:
			return 0;

		// error during retrieving from kfifo
		case 1:
			return -ERR_DEV_WRITE_BUFFER_ERROR;

		// unknown error
		default:
			return -ERR_DEV_WRITE_UNKNOWN;
	}
}

static int uart16550_device_ioctl_set_line(struct uart16550_device* device,
	struct uart16550_line_info info)
{
	// Call hardware set line parameters
	uart16550_hw_set_line_parameters(device->com_port, info);

	// Always success
	return 0;
}

// *****************************************************************************
// **                          BUFFERS MANAGEMENT                             **
// *****************************************************************************

static void uart16550_buffer_init(struct uart16550_buffer* buffer)
{
	// Initialize things
	INIT_KFIFO(buffer->buffer);
	spin_lock_init(&(buffer->mutex_lock));
	sema_init(&(buffer->wait_cond), 0);
	buffer->need_notify_flag = 0;
}

// *****************************************************************************
// **                          MODULE REGISTRATION                            **
// *****************************************************************************
module_init(uart16550_mod_init);
module_exit(uart16550_mod_cleanup);