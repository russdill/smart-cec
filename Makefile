CONFIG ?= t45

CONFIGPATH = configs/$(CONFIG)
include $(CONFIGPATH)/Makefile.inc

PROGRAMMER = -c flyswatter2

CC = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
AVRDUDE = avrdude $(PROGRAMMER) -p $(DEVICE)

CFLAGS = -mmcu=$(DEVICE) -DF_CPU=$(F_CPU)ULL -D_F_CPU=$(F_CPU)
CFLAGS += -Wall -g -Os --std=gnu99
CFLAGS += -ffunction-sections -fdata-sections -fpack-struct
CFLAGS += -fno-inline-small-functions  -fno-move-loop-invariants
CFLAGS += -fno-tree-scev-cprop -fno-move-loop-invariants -fno-tree-scev-cprop
CFLAGS += -Iavr-cec

CFLAGS += -DBOOTLOADER_ADDRESS=0x$(BOOTLOADER_ADDRESS)
CFLAGS += -DCEC_TRANSMIT_PWM
CFLAGS += -DTCNT0_ROLLOVER_HZ=9600*4
CFLAGS += -Wl,--relax
CFLAGS += -DCEC_PUBLIC=static -DIR_NEC_PUBLIC=static -DCEC_TV_PUBLIC=static
CFLAGS += -DUSI_UART_PUBLIC=static -DTIME_PUBLIC=static -DLONG_TIME_S=2
OBJS = main.o
OBJS += ir_nec_isr.o
OBJS += usi_uart_isr.o

all: main.hex test.hex

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

fuse:
	$(AVRDUDE) $(FUSEOPT) -B 20

flash: main.hex keymap.hex
	$(AVRDUDE) -U flash:w:main.hex:i -U eeprom:w:keymap.hex:i -B 20

flashc: combined.hex keymap.hex
	$(AVRDUDE) -U flash:w:combined.hex:i -U eeprom:w:keymap.hex:i -B 20

flashtest: test.hex
	$(AVRDUDE) -U flash:w:$<:i -B 20

test.elf: test.o ir_nec_isr.o usi_uart_isr.o
	$(CC) $(CFLAGS) -o $@ $^
	avr-size $@

main.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	avr-size $@

keymap.hex: lg_cec_keymap.o
	$(OBJCOPY) -j .progmem.data.cec_keymap -O ihex $< $@ --change-address 16

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@
	avr-size $@

%.bin: %.elf
	$(OBJCOPY) -j .text -j .data -O binary $< $@

cec_bl.elf: CFLAGS += -nostartfiles -nostdlib
cec_bl.elf: CFLAGS += -Wl,-Ttext=$(BOOTLOADER_ADDRESS)
cec_bl.elf: cec_bl.o
	$(CC) $(CFLAGS) -o $@ $^
	avr-size $@

run_user.elf: CFLAGS += -nostartfiles -nostdlib
run_user.elf: CFLAGS += -Wl,--just-symbols=main.elf
run_user.elf: run_user.o main.elf
	$(CC) $(CFLAGS) -o $@ $<

run_bl.o: CFLAGS += -DDEST=0x$(BOOTLOADER_ADDRESS)
run_bl.elf: CFLAGS += -nostartfiles -nostdlib
run_bl.elf: run_bl.o
	$(CC) $(CFLAGS) -o $@ $^

combined.hex: main.hex run_user.hex run_bl.hex cec_bl.hex
	srec_cat main.hex -intel -exclude -within run_bl.hex -intel \
		run_bl.hex -intel \
		run_user.hex -intel -exclude -within main.hex -intel \
		cec_bl.hex -intel -o $@ -intel

flashbl: cec_bl.hex
	$(AVRDUDE) -U flash:w:$<:i -B 20

# avr-objdump -j .sec1 -d -m avr5 read.hex
readflash:
	$(AVRDUDE) -U flash:r:read.hex:i -B 20

disasm: main.elf
	$(OBJDUMP) -d $<

disasm_test: test.elf
	$(OBJDUMP) -d $<

clean:
	-rm -f *.{hex,elf,o}
