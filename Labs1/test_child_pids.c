#include <stdio.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <assert.h>
#include <pthread.h>

#define __NR_get_child_pids_id 359
#define N 10

inline long get_child_pids(pid_t* buf, size_t limit, size_t* num_children) {
	return syscall(__NR_get_child_pids_id, buf, limit, num_children) ? errno : 0;
}

void print_list(pid_t* list, size_t limit) {
	int i=0;
	for (; i<limit; i++) {
		printf("list content: index %d value %d \n",i,list[i]);
	}
}

int main () {
	long res;
	int child_pid = fork();
	if (child_pid < 0 )
	    printf("Fork failed %i \n", child_pid);
	else if (child_pid > 0) {
		printf("Child PID after fork is %u\n", child_pid);
		size_t limit = 3;
		size_t nr_children;
		pid_t pid_list[limit]; // to store our results

		// CASE : Arbitrary address for num_children
		res = get_child_pids(pid_list, limit, (size_t*)47424742);
		printf ( "Testing arbitrary address for num_children. Syscall returned %d \n", res);

		// CASE : NULL pid_list, non initialized
		res = get_child_pids(NULL, limit, &nr_children); 
		printf ( "Testing NULL address for pids_list. Syscall returned %d \n", res);
		
		// CASE : Normal execution, num_children < limit
		res= get_child_pids(pid_list, limit, &nr_children) ? errno : 0;
		printf("Testing Nr_children = 1, limit = 3. Syscall returned %d , nr_children is %d\n", res, nr_children);
		printf("LIST OF CHILDREN PIDs syscall\n");
		print_list(pid_list, (nr_children <= limit) ? nr_children : limit);
	}
	return 0;  
}
