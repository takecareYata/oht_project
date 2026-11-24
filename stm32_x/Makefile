TOOLCHAIN ?= arm-none-eabi-
CC := $(TOOLCHAIN)gcc
OBJCOPY := $(TOOLCHAIN)objcopy
SIZE := $(TOOLCHAIN)size
PYTHON ?= python
PROGRAMMER ?= STM32_Programmer_CLI
# Optional ST-Link serial number when multiple boards are connected.
STLINK_SN ?=
BOARD ?= 1
# Z backdoor, board 1 only (see config.h): Z_TIMED=1 time + B1 button, Z_TIMED=2 B1 button only.
Z_TIMED ?= 0
OUT := build/board$(BOARD)$(if $(filter 1,$(Z_TIMED)),_ztimed)$(if $(filter 2,$(Z_TIMED)),_zbutton)
SOURCES := $(wildcard firmware/driver/*.c firmware/service/*.c firmware/app/*.c firmware/bsp/*.c firmware/startup/*.c) firmware/main.c
ASM_SOURCES := firmware/vendor/startup_stm32f411xe.s
OBJECTS := $(patsubst firmware/%.c,$(OUT)/%.o,$(SOURCES)) $(patsubst firmware/%.s,$(OUT)/%.o,$(ASM_SOURCES))
ASFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -g3 -x assembler-with-cpp -MMD -MP
CFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=gnu11 -Os -g3 -Wall -Wextra -Werror -Wconversion -Wshadow -ffunction-sections -fdata-sections -fno-common -MMD -MP -DBOARD_ID=$(BOARD) -DCONFIG_Z_TIMED=$(Z_TIMED)U -DSTM32F411xE -Ifirmware/include -Ifirmware/vendor
LDFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -nostdlib -Tfirmware/linker/stm32f411re.ld -Wl,--gc-sections,-Map=$(OUT)/firmware.map
.PHONY: all both flash clean test
all: $(OUT)/firmware.bin
flash: $(OUT)/firmware.bin
	"$(PROGRAMMER)" -c port=SWD $(if $(STLINK_SN),sn=$(STLINK_SN)) -w "$(OUT)/firmware.elf" -v -rst
both:
	$(MAKE) BOARD=1 all
	$(MAKE) BOARD=2 all
$(OUT)/%.o: firmware/%.c
	@$(PYTHON) -c "from pathlib import Path; Path('$(@D)').mkdir(parents=True,exist_ok=True)"
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/%.o: firmware/%.s
	@$(PYTHON) -c "from pathlib import Path; Path('$(@D)').mkdir(parents=True,exist_ok=True)"
	$(CC) $(ASFLAGS) -c $< -o $@
$(OUT)/firmware.elf: $(OBJECTS) firmware/linker/stm32f411re.ld Makefile
	$(CC) $(LDFLAGS) $(OBJECTS) -lc -lgcc -o $@
	$(SIZE) $@
$(OUT)/firmware.bin: $(OUT)/firmware.elf
	$(OBJCOPY) -O binary $< $@
test:
	$(PYTHON) tools/run_tests.py
clean:
	$(PYTHON) -c "from pathlib import Path; import shutil; p=Path('build').resolve(); assert p.parent==Path.cwd().resolve(); shutil.rmtree(p) if p.exists() else None"
-include $(OBJECTS:.o=.d)
