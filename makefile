# makefile

OS = $(shell uname)
cflags := -std=c90 -Wall -Werror -g -w
lflags := -lreadline
exe := smash

$(exe): smash.c
	$(CC) $(cflags) $^ -o $(exe) $(lflags)

.PHONY: clean

clean:
	rm -rf *.o smash
