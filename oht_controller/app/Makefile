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

.PHONY: all check test clean

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

# 실물 open/ioctl/시간 함수를 교체한 SPI v2 + 중앙 FSM 통합 시험.
test: build/test_tsspi build/test_tcp_stop build/test_spi_debug
	./build/test_tsspi
	./build/test_tcp_stop
	./build/test_spi_debug

build/test_tsspi: ../test/test_tsspi.c TSspi.c TSspi.h TScontrol.c TScontrol.h main.h TStcp.h TSe84.h Makefile | build
	$(CC) $(CPPFLAGS) -std=c99 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=undefined $< -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=clock_gettime -Wl,--wrap=nanosleep -o $@

build/test_tcp_stop: ../test/test_tcp_stop.c TStcp.c TStcp.h TScontrol.h main.h Makefile | build
	$(CC) $(CPPFLAGS) -DTStcp_DEBUG_ENABLED=0 -std=c99 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=undefined ../test/test_tcp_stop.c TStcp.c -o $@

# 큐 포화/출력 지연을 모의하는 진단 전용 시험. 항상 디버그 활성 경로를 검사한다.
build/test_spi_debug: ../test/test_spi_debug.c TSspi.c TSspi.h TScontrol.h main.h TStcp.h Makefile | build
	$(CC) $(CPPFLAGS) -UTSspi_DEBUG_ENABLED -DTSspi_DEBUG_ENABLED=1 -std=c99 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=undefined $< -Wl,--wrap=write -o $@

# 이 Makefile이 생성한 파일만 삭제한다. 빈 build 디렉터리는 유지한다.
clean:
	$(RM) $(TARGET) $(CHECK_TARGET) $(OBJECTS) $(CHECK_OBJECTS) $(DEPFILES) build/test_tsspi build/test_tcp_stop build/test_spi_debug

-include $(DEPFILES)
