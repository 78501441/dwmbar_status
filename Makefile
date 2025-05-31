CC=gcc
CFLAGS=-D_DEBUG -g -O3 -pedantic -std=c89 -Wall -Werror -Wextra -fsanitize=address,alignment,bounds,leak
CFLAGS+=-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
LIBS=-lX11
OUTPUT=dwmbar-status

$(OUTPUT): dwmbar_status.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
clean:
	rm -f *.o $(OUTPUT)
