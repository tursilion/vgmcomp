// Simple Sample for compressed VGM (SPF) playback
// By Tursi

#include "vdp.h"
#include "player30hz.h"
#include "kscan.h"
#include "sound.h" 

// music is inline in here
#include "packed.h"

// Option 3: use the hand tuned asm code directly with register preservation
// This version tags all the GCC registers except 12-15 as clobbered, and GCC
// will have to decide whether to save them based on your code.
// Note it DOES clobber 12-15, I just don't think they need to be preserved.
// If I'm wrong, please do add them. Determine vblank any way you like
// (I recommend VDP_WAIT_VBLANK_CRU), and then include this define "CALL_PLAYER;"
// This is probably the safest for the hand-tuned code
// This calls both the SFX and the Music players
#define CALL_PLAYER \
    __asm__(                                                        \
        "bl @SfxLoop\n\t"                                           \
        "bl @SongLoop"                                              \
        : /* no outputs */                                          \
        : /* no arguments */                                        \
        : "r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r11","cc"   \
        )

int main() {
	unsigned char oldkey = 0;					// prevent key repeat
	unsigned char isPlaying = 0;				// remember if we are playing a song so we can call mute on end

	int x = set_text();							// good old fashioned text mode
	VDP_REG1_KSCAN_MIRROR = x;					// save the value from kscan
	VDP_SET_REGISTER(VDP_REG_COL,(COLOR_WHITE<<4) | COLOR_DKBLUE);	// set foreground white and background color blue

	// load characters
	charsetlc();

	// put up the text menu
	putstring("Music:\n\n");
	putstring(" A - Antarctic Adventure\n");
	putstring(" B - California Games Title\n");
	putstring(" C - Double Dragon\n");
	putstring(" D - Moonwalker\n");
	putstring(" E - Alf\n\n");
	putstring("Low Priority SFX:\n\n");
	putstring(" 1 - Flag\n");
	putstring(" 2 - Hole\n");
	putstring(" 3 - Jump\n\n");
	putstring("High Priority SFX:\n\n");
	putstring(" 4 - Flag\n");
	putstring(" 5 - Hole\n");
	putstring(" 6 - Jump\n\n");
	putstring("7-stop music  8-stop sfx  9-stop all\n");

	VDP_SET_REGISTER(VDP_REG_MODE1,x);		// enable the display

	// disable the music
//	allstopsfx();	// only resets the player
	MUTE_SOUND();	// a good idea to also mute the sound registers

	// set the interrupt routine, disable unneeded processing for extra performance
	VDP_INT_CTRL = VDP_INT_CTRL_DISABLE_SPRITES | VDP_INT_CTRL_DISABLE_SOUND;
	VDP_INT_HOOK = stplay30;

	// now we can main loop
	for (;;) {
		// enable interrupts briefly
		VDP_INT_ENABLE;
		VDP_INT_DISABLE;

		// reset the screen timeout (it counts UP by 2, so set it low and odd)
		VDP_SCREEN_TIMEOUT = 1;

		// check for end of song, and mute audio if so (only if we were
		// playing, this way it doesn't interfere with sound effects)
		// mostly I need this because some of the Sega tunes leave the
		// sound generators running at the end. I did this instead of
		// patching the songs to demonstrate how to deal with it.
		if (isPlaying) {
			if (*pDone == 0) {
				isPlaying=0;
				MUTE_SOUND();
			}
		}

		// use the fast inline KSCAN (hard to get 60 hz with the console kscan)
		kscanfast(0);

		if (KSCAN_KEY != oldkey) {
			switch(KSCAN_KEY) {
			case 'A':
			case 'B':
			case 'C':
			case 'D':
			case 'E':
				MUTE_SOUND();	// in case an old song was still playing (no need to stop it)
				stinit30(music, KSCAN_KEY-'A');
				isPlaying = 1;
				break;

			case '1':
			case '2':
			case '3':
//				sfxinitsfx(music, KSCAN_KEY-'1'+5, 1);
				break;

			case '4':
			case '5':
			case '6':
//				sfxinitsfx(music, KSCAN_KEY-'4'+5, 127);
				break;

			case '7':
//				ststopsfx();
				MUTE_SOUND();
				break;

			case '8':
//				sfxstopsfx();
				break;

			case '9':
//				allstopsfx();
				MUTE_SOUND();
				break;

			// no need for default
			}
			oldkey=KSCAN_KEY;
		}

		// and just as a bit of visualization, and to demonstrate them,
		// display the sound channel feedback on the top row
		// AAAA AA BBBB BB CCCC CC DD DD EEEE
		VDP_SET_ADDRESS(gImage);
		faster_hexprint(pVoice[0]&0xff);	// song channel 1 tone
		faster_hexprint(pVoice[0]>>8);
		VDPWD=' ';
		faster_hexprint(pVol[0]);			// song channel 1 volume
		VDPWD=' ';

		faster_hexprint(pVoice[1]&0xff);	// song channel 2 tone
		faster_hexprint(pVoice[1]>>8);
		VDPWD=' ';
		faster_hexprint(pVol[1]);			// song channel 2 volume
		VDPWD=' ';

		faster_hexprint(pVoice[2]&0xff);	// song channel 3 tone
		faster_hexprint(pVoice[2]>>8);
		VDPWD=' ';
		faster_hexprint(pVol[2]);			// song channel 3 volume
		VDPWD=' ';

		faster_hexprint(pVoice[3]&0xff);	// song noise channel
		VDPWD=' ';
		faster_hexprint(pVol[3]);			// song noise volume
		VDPWD=' ';

		faster_hexprint(*pDone);		// song playing status
		faster_hexprint(*(pDone+1));	// sfx playing status

		// measured, all this (with no key pressed) takes about 4ms
		// of the available 16ms frame, so we should keep up 60hz!
		// if your program can't, you can just enable interrupts
		// at multiple points in your main loop to let the music
		// playback keep up.
	}
}
