CC = gcc
CFLAGS = -fPIC -Wall

.PHONY: build
build: libscheduler.so

libscheduler.so: so_scheduler.o
	$(CC) $(CFLAGS) -shared -o $@ $^

so_scheduler.o: so_scheduler.c so_scheduler.h
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	-rm -f so_scheduler.o libscheduler.so
