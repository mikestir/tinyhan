TARGET = mqttsn_client

OBJS = client_test.o
OBJS += client.o
OBJS += ../tinymac.o ../phy-udp.o

DEBUG_FLAGS = -g
CC = gcc
CFLAGS = -Wall $(DEBUG_FLAGS) -DDEBUG=3 -I..
LDFLAGS = $(DEBUG_FLAGS)

all:	$(TARGET)

$(TARGET):	$(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	
%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@
	
clean:
	rm -rf $(TARGET) $(OBJS)
	
.PHONY:	clean

