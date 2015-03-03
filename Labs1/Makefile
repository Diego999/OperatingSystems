all: test_unique_2e7 test_tree_pids

test_unique_2e7: test_unique_2e7.o
	gcc -o test_unique_2e7 test_unique_2e7.o

test_tree_pids: test_tree_pids.o
	gcc -o test_tree_pids test_tree_pids.o

test_unique_2e7.o: test_unique_2e7.c
	gcc -o test_unique_2e7.o -c test_unique_2e7.c

test_tree_pids.o: test_tree_pids.c
	gcc -o test_tree_pids.o -c test_tree_pids.c

clean:
	rm -f *.o

mrproper: clean
	rm -f test_unique_2e7
	rm -f test_tree_pids