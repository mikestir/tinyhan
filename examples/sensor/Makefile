# Makefile for AVR projects
#
# (C) 2012-2014 Mike Stirling
#

# Output file (prefix)
TARGET=firmware
# Target CPU
CPU=atmega328
# PHY to use for TinyHAN
PHY=si443x
# none, min, float
# NOTE: min version doesn't even support zero-padding of numbers
#PRINTF_VERSION=min

# Fuse settings
# EXT - 0x06
# BODLEVEL[2:0] = 6 (1.8 V)
#
# HIGH - 0xd1
# RSTDISBL = 1 (external reset enabled)
# DWEN = 1 (debugwire disabled)
# SPIEN = 0 (SPI programming enabled)
# WDTON = 1 (WDT not forced on)
# EESAVE = 0 (EEPROM preserved through programming)
# BOOTSZ[1:0] = 0 (1K)
# BOOTRST = 1 (normal reset vector)
#
# LOW - 0x52
# CKDIV8 = 0 (divide clock by 8 initially)
# CKOUT = 1 (disable clock output)
# SUT[1:0] = 1 (BOD enabled)
# CKSEL[3:0] = 2 (internal 8 MHz oscillator)
# 
FUSES_EXT = 0x06
# BOD disabled
#FUSES_EXT = 0x07
FUSES_HIGH = 0xd1
FUSES_LOW = 0x52

# FUSES_HIGH with debugwire enabled
FUSES_HIGH_DW = 0x91

# Target clock frequency (Hz) after any
# changes made to the prescaler on startup
CLOCK = 4000000

# Programmer type
PROG = avrispv2
# Programmer port
PROGPORT = usb
# AVRDUDE extra flags
AVRDUDEFLAGS =

# List directories to search for header and source files
INC_DIRS=.
SRC_DIRS=.

# Object files from which the project is built
OBJECTS=main.o
LIBS=

CFLAGS=-mmcu=$(CPU) -Wall -Wstrict-prototypes -Os -g -std=gnu99
CFLAGS+=-funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums
CFLAGS+=-fdata-sections -ffunction-sections
CFLAGS+=$(addprefix -I,$(INC_DIRS))
CFLAGS+=-DF_CPU=$(CLOCK)

LDFLAGS=-mmcu=$(CPU) -Wl,-Map=$(OUTPUT_DIR)/$(TARGET).map,--cref,--gc-sections
ifeq ($(PRINTF_VERSION),min)
LDFLAGS+=-Wl,-u,vfprintf -lprintf_min
endif
ifeq ($(PRINTF_VERSION),float)
LDFLAGS+=-Wl,-u,vfprintf -lprintf_flt -lm
endif

include tinyhan/tinyhan.mk

# Use := only invokes the shell once instead of on every reference
OUTPUT_DIR:=build-$(TARGET)
OBJS:=$(addprefix $(OUTPUT_DIR)/,$(OBJECTS))

TOOLCHAIN=avr
CC=$(TOOLCHAIN)-gcc
AR=$(TOOLCHAIN)-ar
LD=$(TOOLCHAIN)-ld
OBJCOPY=$(TOOLCHAIN)-objcopy
OBJDUMP=$(TOOLCHAIN)-objdump
NM=$(TOOLCHAIN)-nm
GDB=$(TOOLCHAIN)-gdb
SIZE=$(TOOLCHAIN)-size
RM=rm
FIND=find
MKDIR=mkdir
MAKE=make
AVRDUDE=avrdude

# Search paths
vpath %.c $(SRC_DIRS)
vpath %.s $(SRC_DIRS)

all:	elf hex lss sym

clean:
	$(RM) -rf $(OUTPUT_DIR)

program_fuses:
	$(AVRDUDE) -p $(CPU) -c $(PROG) -P $(PROGPORT) $(AVRDUDEFLAGS) -U efuse:w:$(FUSES_EXT):m
	$(AVRDUDE) -p $(CPU) -c $(PROG) -P $(PROGPORT) $(AVRDUDEFLAGS) -U hfuse:w:$(FUSES_HIGH):m
	$(AVRDUDE) -p $(CPU) -c $(PROG) -P $(PROGPORT) $(AVRDUDEFLAGS) -U lfuse:w:$(FUSES_LOW):m
	
program_flash: $(OUTPUT_DIR)/$(TARGET).hex
	$(AVRDUDE) -p $(CPU) -c $(PROG) -P $(PROGPORT) $(AVRDUDEFLAGS) -U flash:w:$(OUTPUT_DIR)/$(TARGET).hex
	
program_eeprom: $(OUTPUT_DIR)/$(TARGET).eep
	$(AVRDUDE) -p $(CPU) -c $(PROG) -P $(PROGPORT) $(AVRDUDEFLAGS) -U eeprom:w:$(OUTPUT_DIR)/$(TARGET).eep

program: program_fuses program_flash

elf:	$(OUTPUT_DIR)/$(TARGET).elf
hex:	$(OUTPUT_DIR)/$(TARGET).hex
lss:	$(OUTPUT_DIR)/$(TARGET).lss
bin:	$(OUTPUT_DIR)/$(TARGET).bin
sym:	$(OUTPUT_DIR)/$(TARGET).sym
s19:	$(OUTPUT_DIR)/$(TARGET).s19

$(OUTPUT_DIR):
	$(MKDIR) -p $(OUTPUT_DIR)

$(OUTPUT_DIR)/$(TARGET).elf: $(OUTPUT_DIR) $(LINKER_SCRIPT) $(OBJS)
	$(CC) $(LDFLAGS) -o $(OUTPUT_DIR)/$(TARGET).elf $(OBJS) $(LIBS)  
	$(SIZE) $(OUTPUT_DIR)/$(TARGET).elf

$(OUTPUT_DIR)/%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTPUT_DIR)/%.o:	%.s
	$(CC) -xassembler-with-cpp $(CFLAGS) -c -o $@ $<

%.bin:	%.elf
	$(OBJCOPY) --gap-fill=0xff -O binary $< $@

%.hex:	%.elf
	$(OBJCOPY) -O ihex $< $@

%.lss:	%.elf
	$(OBJDUMP) -h -S -j .text -j .io_table -j .cli_table $< > $@

%.sym:	%.elf
	$(NM) -n $< > $@

%.s19:	%.elf
	$(OBJCOPY) -O srec $< $@
	
.PHONY:	program clean
