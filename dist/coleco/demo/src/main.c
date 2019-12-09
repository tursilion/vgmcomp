#include <vdp.h>
#include <kscan.h>
#include <sound.h>
#include "player.h"

#include "packed.h"		// contains the music data

/* VRAM map
   0x0000 - 0x02ff screen image table
   0x0300 - 0x031f color table
   0x0800 - 0x0fff pattern descriptor table
   0x1000 - 0x17ff sprite pattern table
   0x1800 - 0x187f sprite attribute table
*/

const unsigned char arrows[] = {
	0x00,0x30,0x78,0xfc,0xb4,0x30,0x30,0x30,	// up
	0x00,0x30,0x18,0xfc,0xfc,0x18,0x30,0x00,	// right
	0x00,0x30,0x30,0x30,0xb4,0xfc,0x78,0x30,	// down
	0x00,0x30,0x60,0xfe,0xfe,0x60,0x30,0x00,	// left
	0x00,0x78,0xfc,0xfc,0xb4,0x30,0x78,0xfc		// button
};

// code starts
void main(void)
{
	unsigned char oldkey = 0;					// prevent key repeat
	unsigned char isPlaying = 0;				// remember if we are playing a song so we can call mute on end
	unsigned char *p1;
	unsigned char *p2;

	unsigned char x = set_text();				// good old fashioned text mode
	VDP_SET_REGISTER(VDP_REG_COL,(COLOR_WHITE<<4) | COLOR_DKBLUE);	// set foreground white and background color blue

	// load characters
	charsetlc();
	
	// load the graphics (start at char 35 (#))
	vdpmemcpy(gPattern+8*35, arrows, 8*5);

	// put up the text menu
	putstring("Music:\n\n");
	putstring(" $ - Antarctic Adventure\n");
	putstring(" & - California Games Title\n");
	putstring(" % - Double Dragon\n");
	putstring(" # - Moonwalker\n");
	putstring(" ' - Alf\n\n");
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
	allstop();		// only resets the player
	MUTE_SOUND();	// a good idea to also mute the sound registers

	// set the interrupt routine
	setUserIntHook(stplay);

	// now we can main loop
	for (;;) {
		// enable interrupts briefly
		VDP_INT_ENABLE; 
		VDP_INT_DISABLE;

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

		// not sure if kscanfast has any value on the coleco...
		// maybe split them up later though...
		kscan(KSCAN_MODE_LEFT);

		if (KSCAN_KEY == JOY_FIRE) KSCAN_KEY='E';
		if (KSCAN_JOYY == JOY_UP) KSCAN_KEY='D';
		if (KSCAN_JOYY == JOY_DOWN) KSCAN_KEY='C';
		if (KSCAN_JOYX == JOY_LEFT) KSCAN_KEY='B';
		if (KSCAN_JOYX == JOY_RIGHT) KSCAN_KEY='A';

		if (KSCAN_KEY != oldkey) {
			switch(KSCAN_KEY) {
			case 'A':
			case 'B':
			case 'C':
			case 'D':
			case 'E':
				MUTE_SOUND();	// in case an old song was still playing (no need to stop it)
				stinit(packed, KSCAN_KEY-'A');
				isPlaying = 1;
				break;

			case '1':
			case '2':
			case '3':
				sfxinit(packed, KSCAN_KEY-'1'+5, 1);
				break;

			case '4':
			case '5':
			case '6':
				sfxinit(packed, KSCAN_KEY-'4'+5, 127);
				break;

			case '7':
				ststop();
				MUTE_SOUND();
				break;

			case '8':
				sfxstop();
				break;

			case '9':
				allstop();
				MUTE_SOUND();
				break;
				 
			// no need for default
			}
			oldkey=KSCAN_KEY;
		}

		// and just as a bit of visualization, and to demonstrate them,
		// display the sound channel feedback on the top row
		// AAAA AA BBBB BB CCCC CC DD DD EEEE 
		
		// TODO: should not need this. I think BlueMSX is screwing up,
		// it stops firing interrupts yet VDPR1 is still set to 0xF0
		// If I change VDPR1 to disable interrupts, then back, it resumes working.
		// Anyway, it works with this and doesn't hurt anything, except for
		// making testing harder. (It might not be the VDP stuff that is causing
		// it, of course, it could be the controller scanning too fast as well.
		// no VDP access occurs when interrupts are disabled by the flag, but NMIs
		// should still happen!)
		VDP_WAIT_VBLANK_CRU;
		 
		VDP_SET_ADDRESS_WRITE(gImage);
		// we want to print the voices backwards, which is handy, as they are little endian!
		p1 = ((unsigned char*)pVoice);
		p2 = pVol;

		faster_hexprint(*(p1++));		// song channel 1 tone
		faster_hexprint(*(p1++));
		VDPWD=' ';
		faster_hexprint(*(p2++));		// song channel 1 volume
		VDPWD=' ';

		faster_hexprint(*(p1++));		// song channel 2 tone
		faster_hexprint(*(p1++));
		VDPWD=' ';
		faster_hexprint(*(p2++));		// song channel 2 volume
		VDPWD=' ';

		faster_hexprint(*(p1++));		// song channel 3 tone
		faster_hexprint(*(p1++));
		VDPWD=' ';
		faster_hexprint(*(p2++));		// song channel 3 volume
		VDPWD=' ';

		faster_hexprint(*p1);			// song noise channel
		VDPWD=' ';
		faster_hexprint(*p2);			// song noise volume
		VDPWD=' ';

		faster_hexprint(*pDone);		// song playing status
		faster_hexprint(*(pDone-1));	// sfx playing status (little endian!)

	}

}
