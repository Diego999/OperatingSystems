#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/resource.h>

void child1() {
    unsigned i;
    for(i=0; i<1000000000; ++i) {
        if(i % 10000000 == 0) {
            putchar('A');
            fflush(stdout);
        }
	if(i == 300000000) {
	    sleep(1);
	}
    }
}

void child2() {
    unsigned i;
    for(i=0; i<1000000000; ++i) {
        if(i % 10000000 == 0) {
            putchar('B');
            fflush(stdout);
        }
    }
}

int main(int argc, char* argv[]) {
    pid_t child1_pid, child2_pid;

    child1_pid = fork();
    if (child1_pid < 0) {
      return 1;
    } else if (child1_pid == 0) {
      child1();
      exit(0);
    }

    child2_pid = fork();
    if (child2_pid < 0) {
      return 1;
    } else if (child2_pid == 0) {
      child2();
      exit(0);
    }

    setpriority(PRIO_PROCESS, child1_pid, 11);
    setpriority(PRIO_PROCESS, child2_pid, 12);

    return 0;
}
