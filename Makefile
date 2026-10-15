SRCS = src/main.c src/utils.c src/scanner.c src/engine.c src/threadpool.c src/report.c src/ignore.c src/replace.c src/regex_engine.c src/tinf.c src/archive.c src/mcp_server.c src/interactive.c src/semantic.c src/cache.c

ifeq ($(OS),Windows_NT)
    CC ?= gcc
    CFLAGS ?= -O3 -flto=auto -Iinclude -Wall -Wextra -Wno-unused-parameter
    LDFLAGS ?=
    TARGET = bin/supersearch.exe
    MKDIR_BIN = @if not exist bin mkdir bin
    RM_BIN = @if exist bin\supersearch.exe del /f /q bin\supersearch.exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        CC ?= clang
        CFLAGS ?= -O3 -flto -Iinclude -Wall -Wextra -Wno-unused-parameter -pthread
    else
        CC ?= gcc
        CFLAGS ?= -O3 -flto -Iinclude -Wall -Wextra -Wno-unused-parameter -pthread
    endif
    LDFLAGS ?= -pthread
    TARGET = bin/supersearch
    MKDIR_BIN = @mkdir -p bin
    RM_BIN = @rm -f bin/supersearch bin/supersearch.exe
endif

all: $(TARGET)

$(TARGET): $(SRCS)
	$(MKDIR_BIN)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

clean:
	$(RM_BIN)

.PHONY: all clean
