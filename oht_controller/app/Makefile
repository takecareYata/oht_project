.DEFAULT_GOAL := all

CC = gcc
CPPFLAGS ?=
CFLAGS ?= -std=c99 -O2 -g
LDFLAGS ?=
LDLIBS ?=

TARGET := build/oht_controller
CHECK_TARGET := build/check/oht_controller
SOURCES := main.c TScontrol.c TSspi.c TSe84.c TStcp.c
OBJECTS := $(SOURCES:%.c=build/%.o)
CHECK_OBJECTS := $(SOURCES:%.c=build/check/%.o)
DEPFILES := $(OBJECTS:.o=.d) $(CHECK_OBJECTS:.o=.d)

WARNFLAGS := -Wall -Wextra -Werror -Wpedantic \
             -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes \
             -Wmissing-prototypes -Wmissing-declarations -Wcast-qual \
             -Wcast-align=strict -Wformat=2 -Wundef -Wvla -Wnull-dereference \
             -Wswitch-enum -Wswitch-default -Wwrite-strings -Wdouble-promotion \
             -Wpointer-arith -Wbad-function-cast -Wredundant-decls
THREADFLAGS := -pthread
DEPFLAGS := -MMD -MP

.PHONY: all check clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(THREADFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

build/%.o: %.c Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(THREADFLAGS) $(DEPFLAGS) -c $< -o $@

build:
	mkdir -p $@

# GCC 정적 분석을 포함해 별도 산출물로 빌드한다. 프로그램은 실행하지 않는다.
# MISRA 전용 규칙 검사를 대체하지 않는다.
check: $(CHECK_TARGET)

$(CHECK_TARGET): $(CHECK_OBJECTS)
	$(CC) $(LDFLAGS) $(THREADFLAGS) -o $@ $(CHECK_OBJECTS) $(LDLIBS)

build/check/%.o: %.c Makefile | build/check
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(THREADFLAGS) $(DEPFLAGS) -fanalyzer -c $< -o $@

build/check:
	mkdir -p $@

# 이 Makefile이 생성한 파일만 삭제한다. 빈 build 디렉터리는 유지한다.
clean:
	$(RM) $(TARGET) $(CHECK_TARGET) $(OBJECTS) $(CHECK_OBJECTS) $(DEPFILES)

-include $(DEPFILES)
