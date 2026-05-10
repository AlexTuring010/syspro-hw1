CC = gcc
CFLAGS = -Iincludes -Wall -Wextra -g

all: bin/DirList.o bin/TaskQueue.o fss_manager worker fss_console

bin/DirList.o: src/modules/DirList.c | bin
	$(CC) $(CFLAGS) -c $< -o $@

bin/TaskQueue.o: src/modules/TaskQueue.c | bin
	$(CC) $(CFLAGS) -c $< -o $@

fss_manager: src/main/fss_manager.c bin/DirList.o bin/TaskQueue.o
	$(CC) $(CFLAGS) $^ -o $@

fss_console: src/main/fss_console.c 
	$(CC) $(CFLAGS) $^ -o $@

worker: src/main/worker.c
	$(CC) $(CFLAGS) $^ -o $@

bin:
	mkdir -p bin

.PHONY: clean
clean:
	rm -f bin/*
	rm fss_console
	rm worker
	rm fss_manager

