INC_DIRS+=tinyhan
SRC_DIRS+=tinyhan

OBJECTS+=tinymac.o tinyapp.o
ifeq ($(PHY), udp)
OBJECTS+=phy-udp.o
endif
ifeq ($(PHY), si443x)
OBJECTS+=phy-si443x.o
endif

