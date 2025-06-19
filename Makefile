CC=gcc
SANITIZERS=-fsanitize=address,alignment,bounds,leak
CFLAGS+=-g -O3 -pedantic -std=c99 -Wall -Werror -Wextra $(SANITIZERS)
CFLAGS+=-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
LIBS=-lX11
OUTPUT=dwmbar-status

$(OUTPUT): dwmbar_status.c sys_stat_helpers.c xkb_helpers.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
clean:
	rm -f *.o $(OUTPUT)
