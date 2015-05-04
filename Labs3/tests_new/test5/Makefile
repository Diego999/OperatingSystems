CC = gcc
CFLAGS = -DNDEBUG -O3 -Wall

.PHONY: all clean

all: preem

preem: preem.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f preem
