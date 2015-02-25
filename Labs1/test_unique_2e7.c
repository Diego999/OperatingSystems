#include <stdio.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <assert.h>

#define __NR_get_unique_id 358
#define LIMIT 21000001

inline long get_unique_id(int* uuid)
{
	return syscall(__NR_get_unique_id, uuid) ? errno : 0;
}

int main(int argc, char* argv[])
{
	// Declare vars
	int uuid;
	long res;
	int size;
	int i;

	// Welcome message
	printf("Welcome to automated test for the \"get_unique_id\" syscall !\n");

	// Check for first gotten uuid
	printf("    Now checking for current uuid status...\n");
	res = get_unique_id(&uuid);
	if (res != 0)
	{
		printf("        ERROR during syscall, aborting test NOW !\n");
		return -1;
	}
	printf("        syscall returned uuid %d\n", uuid);
	size = LIMIT - uuid;
	if (uuid == 1)
	{
		printf("        test is ready to run, checking %d consecutive uuids\n", size);
		printf("        press ENTER to start...");
	}
	else
	{
		printf("        WARNING to ensure everything is OK, this test should run\n");
		printf("        immediately after a reboot, before any external call to\n");
		printf("        the syscall.\n");
		printf("        if you run the test under current conditions,\n");
		printf("        only %d consecutive uuids will be generated.\n", size);
		printf("        press ENTER to start the test, or Ctrl+C to abort...");
	}

	// Wait for user to press ENTER
	getchar();

	// Run the test
	printf("    Test is running...\n");
	for (i=uuid+1; i<=LIMIT; i++)
	{
		res = get_unique_id(&uuid);
		if (res != 0)
		{
			printf("        ERROR during syscall (supposed to get uuid %d), aborting test NOW !\n", i);
			return -1;
		}
		if (uuid != i)
		{
			printf("        ERROR obtained uuid (%d) does not equal expected uuid (%d).\n", uuid, i);
			printf("              aborting test NOW !\n");
			return -1;
		}
	}

	// Print result
	printf("        SUCCESS\n");
	printf("    Test is finished, program exiting.\n");
	return 0;
}