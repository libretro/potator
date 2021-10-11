TARGET = PotatorPSP
SVROOT = ../../

OBJS = \
 main.o	\
 callbacks.o	\
 vram.o	\
 $(SVROOT)common/controls.o \
 $(SVROOT)common/gpu.o \
 $(SVROOT)common/memorymap.o \
 $(SVROOT)common/sound.o \
 $(SVROOT)common/timer.o \
 $(SVROOT)common/watara.o \
 $(SVROOT)common/m6502/m6502.o

INCDIR = $(SVROOT)common $(SVROOT)common/m6502

CFLAGS = -G0 -Wall -O2 -DPERFORMANCE -DSV_USE_FLOATS
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti $(INCLUDE)
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS= -lintraFont \
 -lpspgu -lpspaudiolib -lpspaudio -lpsppower -lpsprtc -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Potator PSP
PSP_EBOOT_ICON = icon0.png

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
