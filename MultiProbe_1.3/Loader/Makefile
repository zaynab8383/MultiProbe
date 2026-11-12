# Needs arm-none-eabi-gcc (the one that ships with STM32CubeIDE). Run:  make   (from this folder)
CC = arm-none-eabi-gcc
DRV = ../Drivers/CMSIS
CFLAGS = -mcpu=cortex-m7 -mthumb -mfloat-abi=soft -Os -ffreestanding -fno-builtin -fno-exceptions \
         -ffunction-sections -fdata-sections -DSTM32H7B0xx -I../Core/Inc -I$(DRV)/Include \
         -I$(DRV)/Device/ST/STM32H7xx/Include
LDFLAGS = -T loader.ld -nostartfiles -nostdlib -Wl,--gc-sections -Wl,--undefined=Init \
          -Wl,--undefined=Write -Wl,--undefined=SectorErase -Wl,--undefined=MassErase \
          -Wl,--undefined=CheckSum -Wl,--undefined=StorageInfo

all: MultiProbe_W25Q64.stldr
loader.elf: loader.c ../Core/Inc/qspi_ll.h
	$(CC) $(CFLAGS) loader.c $(LDFLAGS) -o $@
MultiProbe_W25Q64.stldr: loader.elf
	cp loader.elf $@
clean:
	rm -f loader.elf MultiProbe_W25Q64.stldr
