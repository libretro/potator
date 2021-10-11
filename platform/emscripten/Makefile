# Emscripten makefile
# Based on https://github.com/aardappel/lobster/blob/master/dev/emscripten/Makefile

CC = emcc

# use -g4 for maximum debug info.
OPTLEVEL = -g4 -O0
CFLAGS = $(OPTLEVEL)
CFLAGS += -Wall -s USE_SDL=2

CSRCS = \
    ../SDL2/main.c \
    ../../common/m6502/m6502.c \
    $(wildcard ../../common/*.c)

COBJS := $(patsubst %.c,%.o,$(CSRCS))

EXPORTED_FUNCTIONS = \
   '_main', \
   '_UploadROM', \
   '_SaveState', \
   '_LoadState', \
   '_NextPalette', \
   '_IncreaseWindowSize', \
   '_DecreaseWindowSize', \
   '_SetVolume', \
   '_SetKey', \
   '_MuteAudio', \
   '_SetGhosting'

.PHONY: potator clean clean2 release all default

# add -s ASSERTIONS=2 when troubleshooting.
#EMCC_WASM_BACKEND=1
potator: $(COBJS)
	$(CC) $(CFLAGS) $(COBJS) \
	  -s "EXTRA_EXPORTED_RUNTIME_METHODS=['ccall']" \
	  -s "EXPORTED_FUNCTIONS=[$(EXPORTED_FUNCTIONS)]" \
	  -o index.html --shell-file potator_shell.html

clean clean2:
	-$(RM) $(COBJS)

release: OPTLEVEL = -O2
release: clean potator clean2

all: potator

default: all
