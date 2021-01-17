cflags := -std=c89 -Wall -Wextra -Werror -g
lflags :=
exe := smash

$(exe): smash.c
	$(CC) $(cflags) $^ -o $(exe) $(lflags)

.PHONY: clean

clean:
	rm -rf *.o smash
