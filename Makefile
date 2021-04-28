all: main
main: main.o indexing.o utils.o 
	gcc -g -std=gnu99 -Wall -o main main.o utils.o indexing.o -lpthread -lm

main.o: main.c library/utils.h library/indexing.h
	gcc -std=gnu99 -Wall -c main.c -lpthread -lm

indexing.o: indexing.c library/utils.h library/indexing.h
	gcc -std=gnu99 -Wall -c indexing.c -lpthread -lm

utils.o: utils.c library/utils.h 
	gcc -std=gnu99 -Wall -c utils.c 

.PHONY: all clean
clean:
	rm main *.o