#include <stdio.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
int main(int argc, char* argv[]) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    if(sched_setaffinity(0, sizeof(mask), &mask) == -1 )
	printf("FAILED TO SET CPU\n");	
	  else
	printf("CPU assigned\n");

    unsigned i;

    if (argc != 2) {
        printf("Usage: loop ch\n");
        return 1;
    }

    char c = argv[1][0];
	
    for(i=0; i<1000000000; ++i) {
        if(i % 10000000 == 0) {
            putchar(c);
            fflush(stdout);
        }
    }
    
    return 0;
}
