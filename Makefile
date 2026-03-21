SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
BIN = comp

CFLAGS  += -Wall -O0 -g3
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
