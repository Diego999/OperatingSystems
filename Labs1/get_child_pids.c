#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

size_t count_children_from_task_struct(struct task_struct* curr) 
{
	struct list_head* pos;
	size_t counter = 0;
	
	task_lock(curr);

	list_for_each(pos, &curr->children)
	{
		printk("%d\n", counter);
		++counter;
	}
	
	task_unlock(curr);

	return counter;
}

void copy_children_from_task_struct(struct task_struct* curr, pid_t* list_dst, size_t n)
{
	struct task_struct* tmp;
	size_t i = 0;

	task_lock(curr);

	list_for_each_entry(tmp, &curr->children, children)
	{
		if(i < n)
			list_dst[i++] = tmp->pid;
	}

	task_unlock(curr);
}

/*
	Return a list of PIDs of a process direct children.
	pid_t* list -> Pointer where the result will be stored after the call
	size_t limit -> Maximum size list buffer can store
	size_t* num_children -> Pointer to the location where the total number of children will be stored after the call
*/
asmlinkage long sys_get_child_pids(pid_t* list, size_t limit, size_t* num_children)
{
	struct task_struct* curr = get_current();

	size_t nb_children = count_children_from_task_struct(curr);

	if(put_user(0, num_children) != 0)
		return -EFAULT;

	if((list == NULL && limit != 0) || (put_user(0, list) != 0))
		return -EFAULT;

	put_user(nb_children, num_children); //Happens in all the cases
	
	if(limit == 0)
	{
		//Sotre only the number of children into num_children
		return 0;
	}
	else if(limit >= nb_children)
	{
		//Copy all children PIDs into list buffer, save the number of children in num_children variable
		copy_children_from_task_struct(curr, list, nb_children);
		return 0;
	}
	else// if(limit < nb_children)
	{
		//Copy the first limit children PIDs into the buffer, save the number of children in num_children
		copy_children_from_task_struct(curr, list, limit);
		return -ENOBUFS;
	}

}
