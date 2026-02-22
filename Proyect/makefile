# Compilador y flags
CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic
LDFLAGS = -lrt -pthread

# Archivos fuente por ejecutable
MONITOR_SRCS = monitor.c comprobador.c pow.c
MINER_SRCS = minero.c pow.c

# Objetos
MONITOR_OBJS = $(MONITOR_SRCS:.c=.o)
MINER_OBJS = $(MINER_SRCS:.c=.o)

# Ejecutables
TARGETS = monitor miner

all: $(TARGETS)

monitor: $(MONITOR_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

miner: $(MINER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGETS)

.PHONY: all clean
