all:
	gcc fcheck.c -o fcheck -Wall -Werror -O -std=gnu11 
clean:
	rm fcheck
