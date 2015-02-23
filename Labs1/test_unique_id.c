#include <stdio.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <assert.h>
#include <pthread.h>

#define __NR_get_unique_id 358
#define N 10

inline long get_unique_id(int* uuid){
	return syscall(__NR_get_unique_id, uuid) ? errno : 0;
}

void* doSomething(void *arg) {
	int uuid[N];
	long res[N];

	int i;
	for(i = 0; i < N; ++i)
		res[i] = get_unique_id(&uuid[i]);
		
	for(i = 0; i < N; ++i)
		printf("tid : %u, id : %d, res : %d\n", pthread_self(), uuid[i], res[i]);
	
}

int main(int argc, char* argv[]) {
	int uuid;
	long res;

	printf("Basic sequential call \n");
	printf("Should return consecutive id and res = 0\n\n");
	int i;
	for(i = 0; i < N; ++i){
		res = get_unique_id(&uuid);
		printf("id : %d, res : %d \n", uuid, res);
	}

	printf("\nBasic sequential call with invalid user-address \n");
	printf("Should return the same id and id = 0 if it is the first time the syscall is called or the same id than the last one. Res shouldn't be = 0\n\n");
	for(i = 0; i < N; ++i) {
		res = get_unique_id((int*)i);//i = invalid addresses for user-space
		printf("id : %d, res : %d \n", uuid, res);
	}

	printf("\nConcurrent call \n Should return different id for each thread and have to be consecutive if you sort them. Res still = 0\n\n");
	pthread_t tid[N];
	for(i = 0; i < N; ++i){
		int err = pthread_create(&(tid[i]), NULL, &doSomething, NULL);
		if(err != 0)
			printf("ERROR DURING THREAD CREATION\n");
	}

	for(i = 0; i < N; ++i)
		pthread_join(tid[i], NULL);
	return 0;
}
