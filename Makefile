SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
BIN = comp

CFLAGS  += -Wall -Wextra -Wno-unused-parameter -Wno-maybe-uninitialized
CFLAGS  += -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS	+= -O1 -g3 -ggdb -std=c99
LDFLAGS +=

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $(LDFLAGS) $^

%.o: %.c
	$(CC) -o $@ -c $(CFLAGS) $^

clean:
	rm -rf $(BIN) $(OBJ)

valgrind: $(BIN)
	valgrind --leak-check=full --track-origins=yes ./$(BIN)
