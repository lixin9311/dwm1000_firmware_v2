# Makefile for the template/example projects in the STM32F0 StdPeriph Driver
# package.
#
# It uses GCC and OpenOCD (or stlink).
#
# Like the readme.txt file in each example directory says (in STM32 StdPeriph
# Driver library), copy the example contents over to the project template
# directory and build.
#
# Tested with Mentor Sourcery ARM GCC toolchain.

# Basename for the resulting .elf/.bin/.hex file
RESULT ?= main

# Path to the STM32F0xx_StdPeriph_Lib_V1.0.0/ directory
TOPDIR = STM32F0xx_StdPeriph_Lib_V1.0.0

SOURCES = \
		$(wildcard platform/*.c) \
		$(wildcard decadriver/*.c) \
		$(wildcard lib/STM32F10x_StdPeriph_Driver/src/*.c) \
	  lib/CMSIS/CM3/CoreSupport/core_cm3.c \
		lib/CMSIS/CM3/DeviceSupport/ST/STM32F10x/system_stm32f10x.c \
		lib/CMSIS/CM3/DeviceSupport/ST/STM32F10x/startup/startup_stm32f10x_hd.s \
		$(wildcard src/*.c)

HEADERS = \
	  $(wildcard platform/*.h ) \
		$(wildcard decadriver/*.h) \
		$(wildcard lib/STM32F10x_StdPeriph_Driver/inc/*.h) \
		lib/CMSIS/CM3/CoreSupport/core_cm3.h \
		lib/CMSIS/CM3/DeviceSupport/ST/STM32F10x/system_stm32f10x.h \
		lib/CMSIS/CM3/DeviceSupport/ST/STM32F10x/stm32f10x.h \
		$(wildcard src/*.h)

LINKER_SCRIPT = Linkers/stm32_flash_256k_ram_64k.ld

INCLUDES += -Iplatform \
			-Idecadriver \
			-Ilib/STM32F10x_StdPeriph_Driver/inc \
			-Ilib/CMSIS/CM3/CoreSupport \
			-Ilib/CMSIS/CM3/DeviceSupport/ST/STM32F10x\
	    -Isrc

CFLAGS += -DUSE_STDPERIPH_DRIVER -DSTM32F103RCT6 -DSTM32F10X_HD -D__ASSEMBLY__
CFLAGS += -fno-common -Wall -O0 -g2 -mcpu=cortex-m3 -mthumb
CFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections
CFLAGS += -lm -lc -lgcc
CFLAGS += $(INCLUDES)
# CFLAGS += -Ibaselibc/include/

CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OBJDUMP = $(CROSS_COMPILE)objdump
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE = $(CROSS_COMPILE)size


# So that the "build depends on the makefile" trick works no matter the name of
# the makefile
THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

all: build size

build: $(RESULT).elf $(RESULT).bin $(RESULT).hex $(RESULT).lst

$(RESULT).elf: $(SOURCES) $(HEADERS) $(LINKER_SCRIPT) $(THIS_MAKEFILE)
	$(CC) -Wl,-M=$(RESULT).map -Wl,-T$(LINKER_SCRIPT) $(CFLAGS) $(SOURCES) -o $@

OBJECTS = $(SOURCES:.c=.o) $(SOURCES:.s=.o)

#$(RESULT).elf: $(OBJECTS) $(HEADERS) $(LINKER_SCRIPT) $(THIS_MAKEFILE)
	#$(CC) -Wl,-M=$(RESULT).map -Wl,-T$(LINKER_SCRIPT) $(CFLAGS) $(OBJECTS) baselibc.a -o $@

%.o: %.c $(HEADERS) $(THIS_MAKEFILE)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s $(HEADERS) $(THIS_MAKEFILE)
	$(CC) $(CFLAGS) -c $< -o $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

%.lst: %.elf
	$(OBJDUMP) -x -S $(RESULT).elf > $@

size: $(RESULT).elf
	$(SIZE) $(RESULT).elf

install: build
	# st-flash write $(RESULT).bin 0x08000000
## Or with openocd (>= v0.6.0)
	openocd -f board/dwm1000.cfg -c "init; reset halt; flash write_image erase $(RESULT).hex; reset run; shutdown"

clean:
	rm -f $(RESULT).elf
	rm -f $(RESULT).bin
	rm -f $(RESULT).map
	rm -f $(RESULT).hex
	rm -f $(RESULT).lst

.PHONY: all build size clean install tx rx
