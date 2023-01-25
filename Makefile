

# Uncomment to compile in parallel
#MAKEFLAGS += -j$(shell nproc || printf 1)

COPT = -O3 -march=core-avx2
CFLAGS += -std=gnu11 -ggdb -Wall -Wextra -Wpedantic -Wunused -Werror -pthread -D_GNU_SOURCE
LDFLAGS += -lm -L src/

#CFLAGS += -D BUILD_VERSION="\"$(shell git describe --dirty --always)\""	-D BUILD_DATE="\"$(shell date '+%Y-%m-%d %H:%M:%S')\""

SRCS = 	src/main.c \
	src/web.c \
	src/fft.c \
	src/lime.c \
	src/timing.c \
	src/buffer/buffer_circular.c

LIBWEBSOCKETS_DIR = src/lib/libwebsockets
LIBWEBSOCKETS_LIBSDIR = ${LIBWEBSOCKETS_DIR}/build/include
LIBWEBSOCKETS_OBJDIR = ${LIBWEBSOCKETS_DIR}/build/lib


LDFLAGS += -ljson-c -lwebsockets -lLimeSuite -lfftw3f

CFLAGS += -I ${LIBWEBSOCKETS_LIBSDIR}

OBJ = ${SRCS:.c=.o}
DEP = ${SRCS:.c=.d}

all: _print_banner src/libwebsockets.a fft-web

debug: COPT = -Og
debug: CFLAGS += -fno-omit-frame-pointer
debug: all

fft-web: ${OBJ}
	@echo "  LD     "$@
	@${CC} ${COPT} ${CFLAGS} -MMD -MP -o $@ ${OBJ} ${LDFLAGS}

%.o: %.c
	@echo "  CC     "$<
	@${CC} ${COPT} ${CFLAGS} -MMD -MP -c -I src/ -fPIC -o $@ $<

src/libwebsockets.a:
	@echo "  CC      libwebsockets"; \
	mkdir -p ${LIBWEBSOCKETS_DIR}/build/; \
	cd ${LIBWEBSOCKETS_DIR}/build/; \
	cmake ../ -DLWS_WITH_SSL=off \
		-DLWS_WITH_SHARED=off \
		-DLWS_WITHOUT_CLIENT=on \
		-DLWS_WITHOUT_TESTAPPS=on; \
	make;
	mv ${LIBWEBSOCKETS_OBJDIR}/libwebsockets.a $@

_print_banner:
	@echo "Compiling with GCC $(shell $(CC) -dumpfullversion) on $(shell $(CC) -dumpmachine)"

clean:
	@rm -rf fft-web ${OBJ} ${DEP}

clean-deps: clean
	@rm -rf ${LIBWEBSOCKETS_DIR}/build/ src/libwebsockets.a

.PHONY: all _print_banner clean clean-deps
