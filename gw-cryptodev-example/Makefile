CFLAGS += -Wall

all: gw-cryptodev-example

gw-cryptodev-example: gw-cryptodev-example.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rfv *.o gw-cryptodev-example
