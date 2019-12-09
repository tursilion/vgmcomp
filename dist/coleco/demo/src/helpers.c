#include <string.h>
#include "cv.h"
#include "cvu.h"
#include "main.h"
#include "helpers.h"
#include "player.h"

volatile bool step;	// Has to be volatile since it's modified in the NMI handler.
static volatile sfr at 0xff audport;

unsigned int seed=0x5512;
// 16 bit seed, but 8 bit random number - be aware
unsigned int rand() {
	if(seed & 1){ /* If the bit shifted out is a 1, perform the xor */
		seed >>= 1;
		seed ^= 0xb400;
	}
	else { /* Else just shift */
		seed >>= 1;
	}

	return(seed>>8);
}

int abs(int x) {
	if (x < 0) {
		return -x;
	}
	return x;
}

// called on vblank from VDP
void nmi(void)
{
	// scramble random number
	rand();

	// release any waiting code
	step = true;

	// update the audio playback
	audiotick();

}

