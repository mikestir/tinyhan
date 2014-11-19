TARGET=sniffer

INC_DIRS=. ../../examples ../../lib
SRC_DIRS=. ../../examples ../../lib

OBJECTS=tinyhan-sniffer.o phy-udp.o

DEBUG_FLAGS=-g -DDEBUG=3

CFLAGS=-Wall -O2 $(DEBUG_FLAGS)
CFLAGS+=-fdata-sections -ffunction-sections
CFLAGS+=$(addprefix -I,$(INC_DIRS))

LDFLAGS=-Wl,--gc-sections

LIBS=-lrt

OUTPUT_DIR:=build-$(TARGET)
OBJS:=$(addprefix $(OUTPUT_DIR)/,$(OBJECTS))

CC=gcc
MKDIR=mkdir
RM=rm

# Search paths
vpath %.c $(SRC_DIRS)

all:	$(OUTPUT_DIR)/$(TARGET)

clean:
	$(RM) -rf $(OUTPUT_DIR)
	
$(OUTPUT_DIR):
	$(MKDIR) -p $(OUTPUT_DIR)

$(OUTPUT_DIR)/$(TARGET):	$(OUTPUT_DIR) $(OBJS)
	$(CC) $(LDFLAGS) -o $(OUTPUT_DIR)/$(TARGET) $(OBJS) $(LIBS)
	
$(OUTPUT_DIR)/%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY:	clean

