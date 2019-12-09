CC = sdcc
CFLAGS = -mz80 -c "-I../../libti99" --std-sdcc99 --vc -DENABLEFX
OLDFLAGS = --opt-code-speed 
AS = "/cygdrive/c/program files (x86)/sdcc/bin/sdasz80"
AFLAGS = -plosgff
# might need to use .o for older SDCC, rel for newer
EXT=rel

.PHONY: all clean

objs = main.$(EXT)

all: build
	"/cygdrive/c/work/coleco/tursi/makemegacart/debug/makemegacart.exe" crt0.ihx sydney.rom
	#(not working) sdobjcopy -S -g -I ihex -O binary crt0.ihx sydney.rom

build: $(objs) crt0.$(EXT)
	$(CC) -mz80 --no-std-crt0 --code-loc 0x8100 --data-loc 0x7000 -l"../../libti99/libti99.a" "./crt0.$(EXT)" main.$(EXT)

main.$(EXT): main.c
	$(CC) $(CFLAGS) main.c

clean:
	rm -f *.rel *.map *.lst *.lnk *.sym *.asm *~ *.o *.obj *.ihx *.sprite.* *.rom *.rel

# Recipe to compile all C files
%.rel: %.c
	$(CC) -c $< $(CFLAGS) -o $@

# Recipe to compile all assembly files
%.rel: %.s
	$(AS) -o $@ $<
