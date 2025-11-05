CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -lpigpio -lpthread -lrt

TARGET = send
SRC = send.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
