CC ?= gcc
PG_INCLUDEDIR = $(shell pg_config --includedir)
CFLAGS ?= -Wall -Wextra
CFLAGS += -I$(PG_INCLUDEDIR)
LDFLAGS += -lpq

TARGET = dbsh
SRC = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
