#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>

asmlinkage long sys_get_unique_id(int *uuid)
{
	// initialize a static counter with lock
	static DEFINE_SPINLOCK(counter_lock);
	static unsigned int counter = 0;

	// initialize kernel-space var for gathering uuid
	unsigned int obtained_uuid = 0;

	// check wether the address is in the user-space memory
	if (put_user(0, uuid) != 0)
		return -EFAULT;

	// increment the counter and get back the result in kernel space (no interrupt)
	spin_lock(&counter_lock);
	counter++;
	obtained_uuid = counter;
	spin_unlock(&counter_lock);

	// put result in user space, interruptable
	put_user(obtained_uuid, uuid);

	// final return
	return 0;
}
