CC= gcc
# IROOT directory based on installed distribution tree (not archive/development tree). 
# IROOT=../..
IROOT=/usr/dalsa/GigeV

INC_PATH = -I. -I$(IROOT)/include -I$(IROOT)/examples/common
                          
DEBUGFLAGS = -g 

CXX_COMPILE_OPTIONS = -c $(DEBUGFLAGS) -DPOSIX_HOSTPC -D_REENTRANT -fno-for-scope \
			-Wall -Wno-parentheses -Wno-missing-braces -Wno-unused-but-set-variable \
			-Wno-unknown-pragmas -Wno-cast-qual -Wno-unused-function -Wno-unused-label

C_COMPILE_OPTIONS= $(DEBUGFLAGS) -fhosted -Wall -Wno-parentheses -Wno-missing-braces \
		   	-Wno-unknown-pragmas -Wno-cast-qual -Wno-unused-function -Wno-unused-label -Wno-unused-but-set-variable


LCLLIBS=  $(COMMONLIBS) -lpthread -lXext -lX11 -L/usr/local/lib -lGevApi -lCorW32

VPATH= . : ../common

%.o : %.cpp
	$(CC) -I. $(INC_PATH) $(CXX_COMPILE_OPTIONS) $(COMMON_OPTIONS) -c $< -o $@

%.o : %.c
	$(CC) -I. $(INC_PATH) $(C_COMPILE_OPTIONS) $(COMMON_OPTIONS) -c $< -o $@


NANO_TRIGGER_OBJS= nano_softwaretrigger_demo.o \
      GevUtils.o \
      convertBayer.o \
      X_Display_utils.o

all: nano_softwaretrigger_demo

nano_softwaretrigger_demo : $(NANO_TRIGGER_OBJS)
	$(CC) -g -o nano_softwaretrigger_demo $(NANO_TRIGGER_OBJS) $(LCLLIBS)



clean:
	rm *.o nano_softwaretrigger_demo 


