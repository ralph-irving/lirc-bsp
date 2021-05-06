CFLAGS = -s -O2 -I../squeezeplay/src/ui -I../squeezeplay/src -I$(PREFIX)/include/SDL -I$(PREFIX)/include
LDFLAGS = -s -L$(PREFIX)/lib -llirc_client -Wl,-rpath,$(PREFIX)/lib

MODULE := ir_bsp.so

all: $(MODULE)

$(MODULE): ir.o ir_bsp.o
	$(CC) $^ -shared $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm -f *.o $(MODULE)
