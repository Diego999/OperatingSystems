#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>

asmlinkage long sys_get_unique_id(int *uuid)
{
	// initialize a static atomic counter
	static atomic_t id = ATOMIC_INIT(0);

	// check wether the address is in the user-space memory
	if (put_user(0, uuid) != 0)
		return -EFAULT;

	// increment the counter
	atomic_inc(&id);
	put_user(atomic_read(&id), uuid);

	// final return
	return 0;
}
