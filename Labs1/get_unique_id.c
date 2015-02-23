#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>

asmlinkage long sys_get_unique_id(int *uuid)
{
	static atomic_t id = ATOMIC_INIT(0);

	//Check whether the address is in the user-space memory
	if(put_user(0, uuid) != 0)
		return -EFAULT;

	atomic_inc(&id);
	put_user(atomic_read(&id), uuid);

	return 0;
}
