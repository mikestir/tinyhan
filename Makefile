TARGET = mqttsn_client

OBJS = mqttsn_client.o packet_udp.o test.o

DEBUG_FLAGS = -g
CC = gcc
CFLAGS = -Wall $(DEBUG_FLAGS) -DDEBUG=3
LDFLAGS = $(DEBUG_FLAGS)

all:	$(TARGET)

$(TARGET):	$(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	
%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@
	
clean:
	rm -rf $(TARGET) $(OBJS)
	
.PHONY:	clean

	