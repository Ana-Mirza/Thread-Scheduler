CC = gcc
CFLAGS = -fPIC -Wall

.PHONY: build
build: libscheduler.so

libscheduler.so: so_scheduler.o
	$(CC) $(LDFLAGS) -shared -o $@ $^

so_scheduler.o: so_scheduler.c ../util/so_scheduler.h
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	-rm -f so_scheduler.o libscheduler.so
