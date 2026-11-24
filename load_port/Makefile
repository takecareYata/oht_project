# Tool information
TOOL_DIR		= C:\arm-gnu-toolchain-15.2.rel1-mingw-w64-i686-arm-none-eabi
VERSION			= 15.2.1
TARGET			= arm-none-eabi

# Project name & Link script
OUT_FILE_NAME 	= rom_0x08000000
LDS_FILE_NAME	= rom_0x08000000.lds

# Compiler & Linker Option, Cortex-M3 & No FPU
CFLAGS  		= -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DSTM32F411xE 
CFLAGS 			+= -std=gnu99 -O3 -Wall -g -c -fno-builtin -funsigned-char -fno-strict-aliasing -fno-common
LDFLAGS 		= -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
LDFLAGS    		+= --specs=nano.specs --specs=nosys.specs -u _printf_float -u _scanf_float -nostartfiles -ffreestanding
LDFLAGS    		+= -Wl,-Map=$(OUT_FILE_NAME).map -Wl,--cref -Wl,-EL -T $(LDS_FILE_NAME)

# Output File definition
OUT_BIN_FILE	= $(OUT_FILE_NAME).bin
OUT_ELF_FILE	= $(OUT_FILE_NAME).elf
OUT_MAP_FILE	= $(OUT_FILE_NAME).map
OUT_DUMP_FILE	= $(OUT_FILE_NAME).dmp

# Tool setting
AS				= "$(TOOL_DIR)/bin/$(TARGET)-as"
CC				= "$(TOOL_DIR)/bin/$(TARGET)-gcc"
LD 				= "$(TOOL_DIR)/bin/$(TARGET)-ld"
OBJCOPY			= "$(TOOL_DIR)/bin/$(TARGET)-objcopy"
OBJDUMP			= "$(TOOL_DIR)/bin/$(TARGET)-objdump"

# Source & Header File (Load Port : 계층별 폴더)
#   l4_app     : L4 Application (main)
#   l3_service : L3 Service     (e84, loadport, system)
#   l2_driver  : L2 Driver      (pio, hcsr04)
#   l1_bsp     : L1 BSP         (tick, clock, exception, runtime, crt0)
#   cmsis      : ST/ARM CMSIS   (레지스터 정의, system_stm32f4xx)
# 오브젝트(.o)는 기존과 같이 이 폴더에 생성된다. (링커 스크립트의 crt0.o 지정 유지)
SRC_DIRS		= app service driver bsp cmsis
CSRC 			= $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
ASRC 			= $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.s))
HEADER 			= $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.h))
OBJS    		= $(notdir $(CSRC:.c=.o)) $(notdir $(ASRC:.s=.o))
DUMP 			= $(wildcard *.txt)

# Library and include folder
C_DIR			= $(TOOL_DIR)/$(TARGET)
GCC_DIR			= $(TOOL_DIR)/lib/gcc/$(TARGET)/$(VERSION)
LIB_OPTION		= -L "$(C_DIR)/lib/thumb2" -L "$(GCC_DIR)/thumb2" -lc -lgcc
INCLUDE			= -nostdinc $(addprefix -I,$(SRC_DIRS)) -I "$(C_DIR)/include" -I "$(GCC_DIR)/include" 

all : $(OUT_BIN_FILE)

$(OUT_BIN_FILE): $(OUT_ELF_FILE)
	$(OBJCOPY) $(OUT_ELF_FILE) $(OUT_BIN_FILE) -O binary
	$(OBJDUMP) -x -D .\$(OUT_ELF_FILE) > .\__dump.txt
	$(OBJDUMP) -x -D -S .\$(OUT_ELF_FILE) > .\__dump_all.txt

$(OUT_FILE_NAME).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIB_OPTION) -o $@

$(OBJS): $(ASRC) $(CSRC) $(HEADER)
	$(CC) $(ASRC) $(CSRC) $(INCLUDE) $(CFLAGS) 

clean :
	rm -f $(OUT_BIN_FILE)
	rm -f $(OUT_ELF_FILE)
	rm -f $(OUT_MAP_FILE)
	rm -f $(OBJS)
	rm -f $(DUMP)

run: # for STM32CubeProgrammer
	STM32_Programmer_CLI.exe -c port=SWD -w ./rom_0x08000000.elf -v -rst -q