
all: blaster blasted

clean:
	rm -f blaster blasted

.PHONY: all clean

%: %.c
	$(CC) -Wall -Werror -o $@ -g -Og $<
