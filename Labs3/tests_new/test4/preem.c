#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sched.h>
void child_body() {
    unsigned i;
    printf("Child started\n");
    for(i=0; i<1000000000; ++i) {
        if(i % 10000000 == 0) {
            putchar('.');
            fflush(stdout);
        }
    }
    printf ("\nChild done\n");
}

int main(int argc, char* argv[]) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    if(sched_setaffinity(0, sizeof(mask), &mask) == -1 )
	printf("FAILED TO SET CPU\n");	
	  else
	printf("CPU assigned\n");


    pid_t child_pid;

    printf("Parent started\n");

    child_pid = fork();
    if (child_pid < 0) {
      return 1;
    } else if (child_pid == 0) {
      child_body();
      exit(0);
    }
   setpriority(PRIO_PROCESS, child_pid, 11); 
  
	  printf("Parent done\n");
    return 0;
}
