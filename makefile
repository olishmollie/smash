cflags := -std=c89 -Wall -Wextra -Werror -Wpedantic -g
lflags :=
exe := smash

$(exe): smash.c
	$(CC) $(cflags) $^ -o $(exe) $(lflags)

debug: smash.c
	$(CC) $(cflags) -DDEBUG $^ -o $(exe) $(lflags)

.PHONY: clean

clean:
	rm -rf *.o smash
