#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <assert.h>
#include <pthread.h>

#define __NR_get_child_pids_id 359
#define NB_LEVELS 3
#define NB_DIRECT_CHILDS_PER_PROCESS 3

/*
 * Syscall wrapper, easing the call to get number of children.
 *
 *   Argument              Description
 *   ---------------------------------------------------------------------------
 *   buf                   array in which store the retrieved pids
 *   limit                 maximum number of children to retrieve (size of array)
 *   num_children          return value of the syscall : total number of children
 *                         of the current process
 */
inline long get_child_pids(pid_t* buf, size_t limit, size_t* num_children)
{
	return syscall(__NR_get_child_pids_id, buf, limit, num_children) ? errno : 0;
}

/*
 * Create processes tree using fork.
 *
 *   Argument              Description
 *   ---------------------------------------------------------------------------
 *   remaining_levels      number of sublevels in the tree under the process
 *                         currently creating create_process. When =0, the
 *                         function immediately return (recursive call stop cond.)
 *   created_children      array in which store created children pids, in order
 *                         to permit further automated tests
 *   creator_idx           index of calling process in the local tree hierarchy
 *                         => tree is stored in a heap-fashion in a linear array
 */
int create_processes(int remaining_levels, pid_t* created_children, int creator_idx)
{
	// local variables
	pid_t pid = 1;
	int i = 0;
	int res = 0;
	int created_idx;

	// if remaining_levels is 0, stop forking
	if (remaining_levels <= 0)
	{
		return -1;
	}

	// loop for creating the subprocesses
	for (i = 0; i<NB_DIRECT_CHILDS_PER_PROCESS; i++)
	{
		pid = fork();
		created_idx = NB_DIRECT_CHILDS_PER_PROCESS*creator_idx + i + 1;
		if (pid > 0)
		{
			created_children[created_idx-1] = pid;
		}
		else if (pid == 0)
		{
			return(create_processes(remaining_levels-1, created_children, created_idx));
		}
	}

	if (remaining_levels < NB_LEVELS)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

/*
 * Compute the expected number of children according to number of levels and
 * size of each level.
 */
size_t compute_num_children_expected()
{
	size_t limit = 0;
	size_t adder = 1;
	int i = 0;
	for (i=0; i<NB_LEVELS; i++)
	{
		adder *= NB_DIRECT_CHILDS_PER_PROCESS;
		limit += adder;
	}
	return limit;
}

/*
 * Print the graphical tree of created children processes, based on the
 * heap-like array containing the pids gathered during processes creation.
 *
 *   Argument              Description
 *   ---------------------------------------------------------------------------
 *   pid_list_expected     list containing the pids stored in a heap-like fashion
 *   num_children_expected number of children that were really created by main
 *                         process
 *   creator_idx           index of the root of current part of the tree, in the
 *                         "heapified" format
 *   indent                indentation management, indicating on which level is
 *                         the root designated by creator_idx
 */
void print_real_tree(pid_t* pid_list_expected, size_t num_children_expected, int creator_idx, int indent)
{
	int i;
	int j;
	int created_idx;
	
	if (indent > NB_LEVELS)
	{
		return;
	}
	for (i=0; i<NB_DIRECT_CHILDS_PER_PROCESS; i++)
	{
		created_idx = NB_DIRECT_CHILDS_PER_PROCESS*creator_idx + i + 1;
		printf(" > ");
		for (j = 0; j<indent; j++)
		{
			printf("   ");
		}
		printf("pid[%d]\n", pid_list_expected[created_idx-1]);
		print_real_tree(pid_list_expected, num_children_expected, created_idx, indent+1);
	}
}

/*
 * Main testing function.
 */
int main(void)
{
	// declare local variables
	size_t num_children_expected = compute_num_children_expected();
	size_t num_children_obtained;
	pid_t* pid_list_expected;
	pid_t* pid_list_obtained = (pid_t*)malloc(num_children_expected * sizeof(pid_t));
	long res;
	int i;
	int j;
	int is_found;
	int shared_mem_identifier;

	// initialize shared memory, allowing processes to communicate in order to automatize test
	printf("initializing shared memory... ");
	shared_mem_identifier = shmget(IPC_PRIVATE, num_children_expected * sizeof(pid_t), IPC_CREAT | 0666);
	if (shared_mem_identifier < 0)
	{
		printf(" ERROR\nQuitting NOW !\n");
		exit(-1);
	}
	else
	{
		pid_list_expected = shmat(shared_mem_identifier, (void*)0, 0);
		if (pid_list_expected == (pid_t*)(-1))
		{
			printf(" ERROR\nQuitting NOW !\n");
			exit(-1);
		}
		else
		{
			printf(" OK !\n");
		}
	}
	
	// create processes
	printf("creating processes... ");
	fflush(stdout);
	if (create_processes(NB_LEVELS, pid_list_expected, 0) == 0)
	{
		// Only the main thread will execute the following of the program, all children threads
		// returns from create_process with a value != 0
		usleep(1000000);
		printf("OK !\n");

		// Print details
		printf("----------------------------------------\n");
		printf("|              PROCESS TREE            |\n");
		printf("----------------------------------------\n");
		printf(" > Here is the generated subprocesses tree, as computed during forks.\n");
		printf(" > root (main)\n");
		print_real_tree(pid_list_expected, num_children_expected, 0, 1);

		// Gather the list from syscall
		printf("\n----------------------------------------\n");
		printf("|            EXECUTE SYSCALL           |\n");
		printf("----------------------------------------\n");
		printf("making the syscall to obtain the list...\n");
		res = get_child_pids(pid_list_obtained, num_children_expected, &num_children_obtained);
		printf("list is ready !\n\n");

		// Print details
		printf("\n----------------------------------------\n");
		printf("|                 RESULTS              |\n");
		printf("----------------------------------------\n");

		// Check return status of the syscall
		switch (res)
		{
			case 0:
				printf(" [  OK  ] syscall returned 0\n");
				break;
			case -ENOBUFS:
				printf(" [ WARN ] syscall returned -ENOBUFS\n");
				break;
			case -EFAULT:
				printf(" [  ERR ] syscall returned -EFAULT\n");
				printf("=> EXITING NOW !\n");
				exit(-1);
			default:
				printf(" [  ERR ] syscall returned an unexpected value (%d)\n", res);
				printf("=> EXITING NOW !\n");
				exit(-1);
		}

		// Check correspondance between expectation and reality for num_children
		if (num_children_obtained == num_children_expected)
		{
			printf(" [  OK  ] %d threads were created, got %d as num_children\n", num_children_expected, num_children_obtained);
		}
		else if (num_children_obtained < num_children_expected)
		{
			printf(" [  ERR ] %d threads were created, only %d were retrieved\n", num_children_expected, num_children_obtained);
			printf("=> EXITING NOW !\n");
			exit(-1);
		}
		else // (num_children_obtained > num_children_expected)
		{
			printf(" [  ERR ] %d threads were created, but %d were retrieved\n", num_children_expected, num_children_obtained);
			printf("=> EXITING NOW !\n");
			exit(-1);
		}

		// Check correspondance between lists of process ids
		printf(" ======== list of children PIDs retrieved :\n");
		for (i=0; i<num_children_obtained; i++)
		{
			is_found = 0;
			for (j=0; j<num_children_expected && is_found==0; j++)
			{
				if (pid_list_obtained[i] == pid_list_expected[j])
				{
					printf(" [  OK  ] process with PID %d has been found by syscall\n", pid_list_obtained[i]);
					is_found = 1;
					pid_list_expected[j] = 0;
				}
			}
			if (is_found == 0)
			{
				printf(" [  ERR ] process with PID %d has been found by syscall but is not a child of current process\n", pid_list_obtained[i]);
				printf("=> EXITING NOW !\n");
				exit(-1);
			}
		}
		printf(" ======== end of list.\n");
		for (i=0; i<num_children_expected; i++)
		{
			if (pid_list_expected[i] != 0)
			{
				printf(" [  ERR ] process with PID %d is a child of current process but was not found by syscall\n", pid_list_expected[i]);
				printf("=> EXITING NOW !\n");
				exit(-1);
			}
		}
		printf(" [  OK  ] EVERYTHING IS OK, TEST SUCCESSFULLY COMPLETED !\n\n");

		// destroy shared memory
		printf("destroying shared memory... ");
		if (shmdt(pid_list_expected) == -1)
		{
			printf(" ERROR\nNow quitting, be aware that the shared memory space is still existing in RAM !\n");
			exit(-1);
		}
		else
		{
			shmctl(shared_mem_identifier, IPC_RMID, NULL);
			printf(" OK !\n");
		}

		// exiting
		free(pid_list_obtained);
		exit(0);
	} else {
		// Wait executed by children threads, in order to maintain them alive
		// during the test is performed by main thread
		usleep(5000000);
	}
}