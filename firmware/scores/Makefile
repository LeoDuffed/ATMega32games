MCU = atmega16
F_CPU = 8000000UL

CC = avr-gcc
OBJCOPY = avr-objcopy
AVRDUDE = avrdude
PROGRAMMER = usbasp

TARGET = main

CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os

all: $(TARGET).hex


$(TARGET).elf: $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<


$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@


flash: $(TARGET).hex
	$(AVRDUDE) -c $(PROGRAMMER) -p m16 -B 20 -U flash:w:$(TARGET).hex:i


fuses-read:
	$(AVRDUDE) -c $(PROGRAMMER) -p m16 \
	-U lfuse:r:-:h \
	-U hfuse:r:-:h \
	-U lock:r:-:h


clean:
	rm -f *.elf *.hex
