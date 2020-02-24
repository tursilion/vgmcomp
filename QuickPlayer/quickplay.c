// Quickplayer for GCC using libti99
// Just a quick example that the Windows tool fills in for easy usage.

#include <vdp.h>
#include <sound.h>
#include <player.h>

// song is loaded by the Windows setup at >A000
#define SONGADR 0xa000

const unsigned char dummy='a';  // works around an assembler bug - just the string overwrites the last byte of code?
                                // todo: good chance this bug was actually in the Windows app... double check.
const unsigned char textout[768+32] = {
    "~~~~DATAHERE~~~~\0"
};

// stplay expects to call from the console interrupt routine, so
// we need a wrapper to restore the workspace pointer
#define WRAP_STPLAY	 __asm__( "bl @stplay\n\tlwpi >8300" );

int main() {
    // setup the display
    set_graphics(VDP_SPR_8x8);
    vdpmemset(gImage, ' ', 768);
    charsetlc();

    // draw the user text - up to 24 nul-terminated strings
    // first byte is a length for centering
    const unsigned char *x = textout;
    for (int r=0; r<23; ++r) {
        unsigned char c= *(x++);
        if (c == 0) break;
        if (c > 31) c = 0;
        c = (32-c)/2;
        unsigned int adr = VDP_SCREEN_POS(r,c);
        VDP_SET_ADDRESS(adr);
        while (*x) {
            VDPWD = *(x++);
        }
        ++x;
    }

    // start the song
    stinit((const void*)SONGADR, 0);

    // and play it on the interrupt
    for (;;) {
		vdpwaitvint();	// waits for a console interrupts, allows quit/etc
		if (*pDone) {
			WRAP_STPLAY;
		} else {
			MUTE_SOUND();
		}
    }

    // never reached
    return 2;

}
