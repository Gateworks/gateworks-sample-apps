CFLAGS += -Wall

all: gw-latency-tester

gw-latency-tester: gw-latency-tester.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rfv *.o gw-latency-tester
