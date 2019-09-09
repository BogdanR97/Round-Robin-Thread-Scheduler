build: libscheduler.so

libscheduler.so: so_scheduler.o
	gcc -Wall -g -shared so_scheduler.o -o libscheduler.so

so_scheduler.o: so_scheduler.c
	gcc -Wall -g -fPIC -c so_scheduler.c -lpthread -lrt
	
clean:
	rm libscheduler.so
	rm so_scheduler.o
