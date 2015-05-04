CC = gcc
CFLAGS = -DNDEBUG -O3 -Wall -D_GNU_SOURCE

.PHONY: all clean

all: yield

yield: yield.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f yield
