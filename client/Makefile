TARGET = mqttsn_client

OBJS = main.o
OBJS += client.o
OBJS += tinymac-node.o ../phy-udp.o ../timer-linux.o

DEBUG_FLAGS = -g
CC = gcc
CFLAGS = -Wall $(DEBUG_FLAGS) -DDEBUG=3 -I..
LDFLAGS = $(DEBUG_FLAGS)
LIBS = -lrt

all:	$(TARGET)

$(TARGET):	$(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	
%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@
	
clean:
	rm -rf $(TARGET) $(OBJS)
	
.PHONY:	clean

