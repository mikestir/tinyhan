TARGET=simplegateway
PHY=udp

INC_DIRS=. ..
SRC_DIRS=. ..

OBJECTS=main.o

DEBUG_FLAGS=-g -DDEBUG=3

CFLAGS=-Wall -O2 $(DEBUG_FLAGS)
CFLAGS+=-fdata-sections -ffunction-sections
CFLAGS+=$(addprefix -I,$(INC_DIRS)) -DWITH_TINYMAC_COORDINATOR=1

LDFLAGS=-Wl,--gc-sections

LIBS=-lrt

include tinyhan/tinyhan.mk

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

