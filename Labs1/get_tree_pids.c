#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

/******************************************************************************/
/*                            DEBUGGING SECTION                               */
/******************************************************************************/

// Debug/Release flag -> select only one !
//#define DEBUG        // select DEBUG (uncomment) to make entire syscall
                       // generate debugging messages in kernel console (printk)
#define RELEASE        // select RELEASE (uncomment) to make entire syscall
                       // be silent (production)

// Debug message generation macro
#ifdef DEBUG
#define PRINT_DEBUG(...) printk(__VA_ARGS__)
#endif // DEBUG
#ifdef RELEASE
#define PRINT_DEBUG(...)
#endif // RELEASE

/******************************************************************************/
/*                           FUNCTION PROTOTYPES                              */
/******************************************************************************/

void extract_tree_pids(
	struct task_struct* _in_ker_ptr_root_proc,
	size_t _in_ker_val_array_max_size,
	pid_t* _out_ker_ptr_array_pids,
	size_t* _out_ker_ptr_real_sub_number);

void traverse_tree_rec(
	struct task_struct* _in_ker_ptr_root_proc,
	size_t* _io_ker_ptr_array_free_space,
	size_t* _io_ker_ptr_array_current,
	size_t* _io_ker_ptr_total_nodes_count,
	pid_t* _out_ker_ptr_array_pids);

long copy_array_from_kernel_to_user(
	pid_t* _in_ker_ptr_array_pids,
	size_t _in_ker_val_array_size,
	pid_t* _out_usr_ptr_array_pids);

long copy_number_from_kernel_to_user(
	size_t* _in_ker_ptr_real_sub_number,
	size_t* _out_usr_ptr_real_sub_number);

/******************************************************************************/
/*                          MAIN SYSCALL FUNCTION                             */
/******************************************************************************/

/*
 * Main syscall function. Returns all the pids for children of calling process
 * including children of children, etc... recursively.
 *
 *   Argument         Dir  Space   Type   Description
 *   ---------------------------------------------------------------------------
 *   list             out  user    ptr    list of children pids
 *   limit            in   kernel  val    max number of children to gather
 *   num_children     out  user    ptr    total number of children pids
 *                                        discovered
 */
asmlinkage long sys_get_child_pids(
	pid_t* list,
	size_t limit,
	size_t* num_children)
{
	// local variables
	pid_t ker_list[limit];
	size_t ker_num_children;
	struct task_struct* curr;

	PRINT_DEBUG("beginning of sys_get_child_pids\n");

	// Check that if list (user space) is NULL, then limit = 0
	if (list == NULL && limit != 0)
	{
		PRINT_DEBUG(" -> list is null, but limit = %d != 0, exiting\n",
			limit);
		return -EFAULT;
	}

	// Check that num_children (user space) is writtable
	if (put_user(0, num_children) != 0)
	{
		PRINT_DEBUG(" -> num_children is not writtable, exiting\n");
		return -EFAULT;
	}

	// Check that the first position of list is writtable
	if (put_user(0, list) != 0)
	{
		PRINT_DEBUG(" -> list[0] is not writtable, exiting\n");
		return -EFAULT;
	}

	// Get current process
	PRINT_DEBUG(" -> gathering current process\n");
	curr = get_current();

	// Gather full tree into ker_list, real number into ker_num_children
	PRINT_DEBUG(" -> ready to start gathering tree\n");
	extract_tree_pids(
		curr,
		limit,
		ker_list,
		&ker_num_children);
	PRINT_DEBUG(" -> tree has been captured\n");

	// Put children number in user space (happens in any case)
	PRINT_DEBUG(" -> copying number of children (%d) to user space\n",
		ker_num_children);
	if (copy_number_from_kernel_to_user(
			&ker_num_children,
			num_children) != 0)
	{
		return -EFAULT;
	}

	// if limit is 0, only num_children had to be copied, just return
	if (limit == 0)
	{
		PRINT_DEBUG(" -> limit is zero, end of sys_get_child_pids\n");
		return 0;
	}

	// if the array is bigger than the number of children, copy all children
	// then exit with success
	else if (limit >= ker_num_children)
	{
		PRINT_DEBUG(" -> limit (%d) >= num_children (%d),", limit,
			ker_num_children);
		PRINT_DEBUG(" copying %d pids to user space\n", ker_num_children);
		if (copy_array_from_kernel_to_user(
				ker_list,
				ker_num_children,
				list) != 0)
		{
			return -EFAULT;
		}
		PRINT_DEBUG("end of sys_get_child_pids\n");
		return 0;
	}

	// if the array is smaller than the number of children, copy only the first
	// "limit" children, then exit with ENOBUFS
	else if (limit < ker_num_children)
	{
		PRINT_DEBUG(" -> limit (%d) < num_children (%d),", limit,
			ker_num_children);
		PRINT_DEBUG(" copying %d pids to user space\n", limit);
		if (copy_array_from_kernel_to_user(
				ker_list,
				limit,
				list) != 0)
		{
			return -EFAULT;
		}
		PRINT_DEBUG("end of sys_get_child_pids\n");
		return -ENOBUFS;
	}

	PRINT_DEBUG("end of sys_get_child_pids with unknown error\n");
	return -EFAULT;
}

/******************************************************************************/
/*                             OTHER FUNCTIONS                                */
/******************************************************************************/

/*
 * Extract the entire list of pids of all (sub*)childs of given process, with an
 * upper bound limit.
 *
 *   Argument         Dir  Space   Type   Description
 *   ---------------------------------------------------------------------------
 *   root_proc        in   kernel  ptr    task_struct of the root process
 *   array_max_size   in   kernel  val    maximum number of children to gather
 *                                        pids from
 *   array_pids       out  kernel  ptr    array of pids, at most of size
 *                                        max_list_size
 *   real_sub_number  out  kernel  ptr    total number of children in tree (may
 *                                        be > max_list_size)
 */
void extract_tree_pids(
	struct task_struct* _in_ker_ptr_root_proc,
	size_t _in_ker_val_array_max_size,
	pid_t* _out_ker_ptr_array_pids,
	size_t* _out_ker_ptr_real_sub_number)
{
	// local variables
	size_t array_free_space = _in_ker_val_array_max_size;
	size_t array_current = 0;

	PRINT_DEBUG("beginning of extract_tree_pids\n");

	// initialize out total counter (real_sub_number) to 0
	*(_out_ker_ptr_real_sub_number) = 0;

	// start recursion cascade through the entire subtree of root_proc
	traverse_tree_rec(
		_in_ker_ptr_root_proc,
		&array_free_space,
		&array_current,
		_out_ker_ptr_real_sub_number,
		_out_ker_ptr_array_pids);

	PRINT_DEBUG("end of extract_tree_pids\n");

	// final return
	return;
}

/*
 * Fills in an array of pids of all (sub*)childs of given process, using
 * recursion. The algo is depth-first, and counts ALL children nodes of the
 * given root process, even if they don't fit in the list.
 *
 *   Argument         Dir  Space   Type   Description
 *   ---------------------------------------------------------------------------
 *   root_proc        in   kernel  ptr    task_struct of the root process
 *   array_free_space i/o  kernel  ptr    the remaining pids that can fit into
 *                                        array_pids
 *   array_current    i/o  kernel  ptr    the index of array_pids in which write
 *                                        the next pid, if array_free_space > 0
 *   total_nodes_count i/o kernel  ptr    total number of children of root_proc,
 *                                        counter also when array_free_space
 *                                        falls to 0
 *   array_pids       out  kernel  ptr    array of pids
 */
void traverse_tree_rec(
	struct task_struct* _in_ker_ptr_root_proc,
	size_t* _io_ker_ptr_array_free_space,
	size_t* _io_ker_ptr_array_current,
	size_t* _io_ker_ptr_total_nodes_count,
	pid_t* _out_ker_ptr_array_pids)
{
	// local variables
	struct task_struct* direct_child;

	PRINT_DEBUG("beginning of traverse_tree_rec\n");

	// lock the root task
	task_lock(_in_ker_ptr_root_proc);

	// go through the list of children of the root
	list_for_each_entry(direct_child, &(_in_ker_ptr_root_proc->children),
		sibling)
	{
		PRINT_DEBUG("found a child, pid is %d\n", direct_child->pid);

		// increment the total count of children
		*(_io_ker_ptr_total_nodes_count) += 1;

		// if there is enough space in the output pids list
		if (*(_io_ker_ptr_array_free_space) > 0)
		{
			PRINT_DEBUG(" -> free space available, adding it to array\n");

			// add the pid to the output array
			_out_ker_ptr_array_pids[*(_io_ker_ptr_array_current)]
				= direct_child->pid;

			// decrement free space, increment current index
			*(_io_ker_ptr_array_free_space) -= 1;
			*(_io_ker_ptr_array_current) += 1;
		}

		PRINT_DEBUG(" -> calling traverse_tree_rec recursively on it\n");

		// call recursively on the children
		traverse_tree_rec(
			direct_child,
			_io_ker_ptr_array_free_space,
			_io_ker_ptr_array_current,
			_io_ker_ptr_total_nodes_count,
			_out_ker_ptr_array_pids);
	}

	// unlock the root task
	task_unlock(_in_ker_ptr_root_proc);

	PRINT_DEBUG("end of traverse_tree_rec\n");

	// final return
	return;
}

/*
 * Copy the pids array gathered in kernel space to the corresponding var in user
 * space.
 *
 *   Argument         Dir  Space   Type   Description
 *   ---------------------------------------------------------------------------
 *   array_pids       in   kernel  ptr    array of pids, in kernel space
 *   array_size       in   kernel  val    number of pids to copy
 *   array_pids       out  user    ptr    destination array for pids, in user
 *                                        space
 */
long copy_array_from_kernel_to_user(
	pid_t* _in_ker_ptr_array_pids,
	size_t _in_ker_val_array_size,
	pid_t* _out_usr_ptr_array_pids)
{
	// local vars
	size_t i;
	int put_user_ret;

	PRINT_DEBUG("beginning of copy_array_from_kernel_to_user\n");

	// writes all pids in order, halting immediately on error
	for (i=0; i<_in_ker_val_array_size; i++)
	{
		// put in user space
		put_user_ret = put_user(
			_in_ker_ptr_array_pids[i],
			_out_usr_ptr_array_pids + i);

		// if an error occurred, display and abort
		if (put_user_ret != 0)
		{
			PRINT_DEBUG(" -> at i=%d, put_user returned with non-zero", i);
			PRINT_DEBUG(" status %d, exiting\n", put_user_ret);
			return (-1) * put_user_ret;
		}
	}

	PRINT_DEBUG("end of copy_from_kernel_to_user\n");

	// final return
	return 0;
}

/*
 * Copy the number of children gathered in kernel space to the corresponding
 * var in user space.
 *
 *   Argument         Dir  Space   Type   Description
 *   ---------------------------------------------------------------------------
 *   real_sub_number  in   kernel  ptr    real number of children
 *   real_sub_number  out  user    ptr    destination of real number of children
 */
long copy_number_from_kernel_to_user(
	size_t* _in_ker_ptr_real_sub_number,
	size_t* _out_usr_ptr_real_sub_number)
{
	// local vars
	int put_user_ret;

	PRINT_DEBUG("beginning of copy_number_from_kernel_to_user\n");

	// put in user space
	put_user_ret = put_user(
		*(_in_ker_ptr_real_sub_number),
		_out_usr_ptr_real_sub_number);

	// if an error occurred, display and abort
	if (put_user_ret != 0)
	{
		PRINT_DEBUG(" -> put_user returned with non-zero ");
		PRINT_DEBUG("status %d, exiting\n", put_user_ret);
		return (-1) * put_user_ret;
	}

	PRINT_DEBUG("end of copy_from_kernel_to_user\n");

	// final return
	return 0;
}