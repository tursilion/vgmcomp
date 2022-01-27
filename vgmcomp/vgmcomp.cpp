// vgmcomp.cpp : Defines the entry point for the console application.
//

// one remaining issue (minor) is the output of two blank frames at 
// the very end of the output - they seem to come from the compression
// stage. We will see if it's important later. (Might be the decompress
// test adding them...)

#include "stdafx.h"
#include <stdarg.h>
#define MINIZ_HEADER_FILE_ONLY
#include "tinfl.c"						// comes in as a header with the define above
										// here to allow us to read vgz files as vgm

#define MAXTICKS 432000					// about 2 hrs, but arbitrary
#define MAXSONGS 16						// arbitrary
#define MAX_RUN_SIZE	63				// do not make larger than 63, other bits reserved

// test code
// this tries searching forwards as well as backwards. It tends to be 2-3 times
// larger on big streams and 50% smaller on little streams. Bug, bad estimation?
// not sure, but it's not better right now. 
// Corrupts some songs too.
//#define TESTPACK

// 8 huge channels
int VGMStream[MAXSONGS*8][MAXTICKS];		// every frame the data is output
unsigned char buffer[1024*1024];			// 1MB read buffer
unsigned int buffersize;					// number of bytes in the buffer
int nCurrentTone[8];						// current value for each channel in case it's not updated
bool deadStream[4*MAXSONGS];				// flag streams that have no tone or volume on them so we leave them blank
bool undeadStream3 = false;					// true if stream 3 is manipulating the noise channel, so should not be considered dead
unsigned char OutStream[MAXSONGS*8][MAXTICKS];	// note/vol/note/vol/note/vol/noise/vol
int OutPos[MAXSONGS*8];
int TimePos[MAXSONGS*4];
unsigned char TimeStream[MAXSONGS*4][MAXTICKS];
double freqClockScale = 1.0;			// doesn't affect noise channel, even though it would in reality. Not much we can do!
int nTicks[MAXSONGS];
int maxTicks = 0;
bool verbose = false;
bool lossyslide = false;
int nDesiredSong = -1;
int minRunLoop = 3;
int maxRunLoop = 20;

// TODO: test code to see if it's worth it
// assumes all refs are the same size, which isn't true
int backrefCount[65536];

unsigned char OutputBuf[65536*2];		// 64k is max size, but we leave slack for the compressor (even 64k is too big for the TI anyway)
unsigned char OutputBuf0[65536*2];		// curstream searches on
unsigned char OutputBuf1[65536*2];		// curstream searches per stream determined

// used frequency lookup table (64k to start - 256 maximum at end)
int Freqs[65536];

// musical note table (from Editor/Assembler manual) - used in the lossy note reduction code
int musicalnotes[] = {
	1017,960,906,855,807,762,719,679,641,605,571,539,
	508,480,453,428,404,381,360,339,320,302,285,269,
	254,240,226,214,202,190,180,170,160,151,143,135,
	127,120,113,107,101,95,90,85,80,76,71,67,64,60,
	57,53,50,48,45,42,40,38,36,34,32,30,28,27,25,24,
	22,21,20
};

// options
bool scaleFreqClock = false;			// apply scaling for unexpected clock rates
bool halfvolrange = false;				// use only 8 levels of volume instead of 16 for better compression
bool packVolumes = false;				// volume changes are packed two to a byte instead of one - requires packed player
bool code30hz = false;					// code for 30hz instead of 60 hz
int forceRun = MAX_RUN_SIZE;			// how many bytes at the beginning of each stream to leave uncompressed

int numSongs=0;							// number of songs the user has asked us to encode

#define ABS(x) ((x)<0?-(x):x)

// crc function for checking the gunzip
unsigned long crc(unsigned char *buf, int len);

// printf replacement I can use for verbosity ;)
void myprintf(const char *fmt, ...) {
	if (verbose) {
		va_list lst;
		va_start(lst, fmt);
		vfprintf(stdout, fmt, lst);
		va_end(lst);
	} else {
		static int spin=0;
		const char anim[] = "|/-\\";
		printf("\r%c", anim[spin++]);
		if (spin >= 4) spin=0;
	}
}

// try to unpack buffer as a gzipped vgm (vgz) - note that the extension
// is not always vgz in practice! I've seen gzipped vgm in the wild
// using miniz's "tinfl.c" as the decompressor
// returns difference between new length and buffersize so the caller
// can update statistics
int tryvgz() {
	size_t outlen=0;
	unsigned int npos = 0;
	myprintf("Signature not detected.. trying gzip (vgz)\n");
	// we have to parse the gzip header to find the packed data, then tinfl
	// can handle it. (miniz doesn't support gzip headers)
	// gzip header is pretty simple - especially since we don't care about much of it
	// see RFC 1952 (may 1996)
	if (buffersize < 10) {
		printf("\rToo short for GZIP header\n");
		return 0;
	}
	// 0x1f 0x8f -- identifier
	if ((buffer[npos]!= 0x1f)||(buffer[npos+1]!=0x8b)) {
		printf("\rNo GZIP signature.\n");
		return 0;
	}
	npos+=2;
	// compression method - if not '8', it's not gzip deflate
	if (buffer[npos] != 8) {
		printf("\rNot deflate method.\n");
		return 0;
	}
	npos++;
	// flags - we mostly only care to see if we need to skip other data
	unsigned int flags = buffer[npos++];
	// modification time, don't care
	npos+=4;
	// extra flags, don't care
	npos++;
	// OS, don't care
	npos++;
	// optional extra header
	if (flags&0x04) {
		// two byte header, then data
		int xlen = buffer[npos] + buffer[npos+1]*256;
		// don't care though
		npos+=2+xlen;
	}
	// optional filename
	if (flags & 0x08) {
		// zero-terminated ascii
		while ((npos < buffersize) && (buffer[npos] != 0)) npos++;
		npos++;
	}
	// optional comment
	if (flags & 0x10) {
		// zero-terminated ascii
		while ((npos < buffersize) && (buffer[npos] != 0)) npos++;
		npos++;
	}
	// header CRC
	if (flags & 0x02) {
		// two bytes of CRC32
		npos+=2;
		printf("\rWarning, skipping header CRC\n");
	}
	// unknown flags
	if (flags & 0xe0) {
		printf("\rUnknown flags, decompression may fail.\n");
	}

	// we should now be pointing at the data stream
	// AFTER the data stream, the last 8 bytes are
	// 4 bytes of CRC32, and 4 bytes of size, both
	// referring to the original, uncompressed data

	void *p = tinfl_decompress_mem_to_heap(&buffer[npos], buffersize-npos, &outlen, 0);
	if (p == NULL) {
		printf("\rDecompression failed.\n");
		return 0;
	}

	// check CRC32 and uncompressed size
	unsigned int crc32=buffer[buffersize-8]+buffer[buffersize-7]*256+buffer[buffersize-6]*256*256+buffer[buffersize-5]*256*256*256;
	unsigned int origsize=buffer[buffersize-4]+buffer[buffersize-3]*256+buffer[buffersize-2]*256*256+buffer[buffersize-1]*256*256*256;
	if (origsize != outlen) {
		printf("\rWARNING: output size does not match file\n");
	} else {
		if (crc32 != crc((unsigned char*)p, outlen)) {
			printf("\rWARNING: GZIP CRC does not match output data\n");
		}
	}

	if (outlen > sizeof(buffer)) {
		printf("\rResulting file larger than buffer size (1MB), truncating.\n");
		outlen=sizeof(buffer);
	}
	printf("\rDecompressed to %d bytes\n", outlen);
	int ret = outlen - buffersize;
	memcpy(buffer, p, outlen);
	buffersize=outlen;
	free(p);
	return ret;
}

#ifdef TESTPACK
// the idea seems good - look for the strings globally rather than only backwards, and try to keep the
// best ones. On the smaller streams it seemed all right, sometimes halving the size, but on all larger
// ones it did substantially worse. That may be an error in this algorithm, or I may have just stumbled
// into a pretty good (for the requirements) pack routine as it was, luck or not.

// structure for testpack
#pragma pack(1)
struct _res {
	int start;
	int len;
	int cnt;
	int score;
};

void TestPack(unsigned char *pSrc, int nSize) {
	// this approach generates a list of strings and checks
	// which ones are the best candidates to keep, assuming
	// that longer strings with more hits are most valuable
	// dumb search like the old one, but there's a good chance
	// it only needs to run once
	struct _res *results;
	int nLastrun = 0;

	myprintf("Trying experimental pack...\n");

	// lengths run from 4 to 20, so there are 17*bytes_in_file possible strings
	results=(struct _res*)malloc(17*nSize*sizeof(struct _res));		// 2D array, size x start
	for (int len = 4; len <= 20; len++) {
		for (int p1=0; p1<nSize-len; p1++) {
			results[(len-4)*nSize+p1].cnt = 0;
			results[(len-4)*nSize+p1].len = len;
			results[(len-4)*nSize+p1].start = p1;
			results[(len-4)*nSize+p1].score = 0;
		}
	}

	for (int len = 4; len <= 20; len++) {
		myprintf("Counting length %d\r", len);
		for (int p1=0; p1<nSize-len; p1++) {
			for (int p2=p1+len; p2<nSize-len; p2++) {
				if (0 == memcmp(&pSrc[p1], &pSrc[p2], len)) {
					++results[(len-4)*nSize+p1].cnt;	// gain a point for fwdref
					--results[(len-4)*nSize+p2].cnt;	// lose a point in favor of backref
				}
			}
		}
	}
	myprintf("\n");

	// now we have a list of 16 counts for each position indicating how useful it is to later strings
	// convert to best scores (count * length) and condense to 1D array
	myprintf("Scoring...\n");
	for (int len = 4; len <= 20; len++) {
		for (int p1=0; p1<nSize-len; p1++) {
			int score = results[(len-4)*nSize+p1].cnt * len;	// don't need to multiply by len-4 since it's relative anyway
			if (score > results[p1].cnt*results[p1].len) {
				// copy array
				results[p1] = results[(len-4)*nSize+p1];
			}
		}
	}
	// now tally up total scores - a run's score is improved by the scores of
	// any run it entirely contains
	for (int p1=0; p1<nSize-4; p1++) {
		int score = 0;
		for (int p2=p1; p2<p1+results[p1].len; p2++) {
			if (p2 >= nSize-4) break;
			if (results[p2].len+p2 >= p1+results[p1].len) continue;		// not entirely enclosed
			score += results[p2].len * results[p2].cnt;
		}
		results[p1].score = score;
	}

	// now keep the best out of each run
	myprintf("Trimming...\n");
	for (int p1=0; p1<nSize-4; p1++) {
		int p2;

		if (results[p1].score < 1) continue;

		for (p2=p1+1; p2<p1+results[p1].len; p2++) {
			if (results[p2].score > results[p1].score) {
				// current one is not as cool as one in the streamm, so zero it
				results[p1].score = 0;
				break;
			}
		}
		if (p2 >= p1+results[p1].len) {
			// current one beats all its contents, so zero contents
			for (p2=p1+1; p2<p1+results[p1].len; p2++) {
				results[p2].score = 0;
			}
		}
	}

	// now check the remaining ones -- any who line up with an earlier one
	// that we are keeping can also be dropped
	for (int p1=0; p1<nSize-4; p1++) {
		if (results[p1].score <= 0) continue;
		if (p1 + results[p1].len >= nSize-4) continue;

		for (int p2=p1+1; p2<nSize-4; p2++) {
			if (results[p2].score <= 0) continue;
			if (p2 + results[p2].len >= nSize-4) continue;

			for (int off = 0; off < 20; off++) {
				if (p1+off+results[p2].len >= nSize-4) continue;
				if (0 == memcmp(&pSrc[p1+off], &pSrc[p2], results[p2].len)) {
					// it's a perfect match, just drop it
					results[p2].score = 0;
					break;
				}
			}
		}
	}

	// just some output to see what it came up with...
	myprintf("Keepers...\n");
	for (int p1=0; p1<nSize-4; p1++) {
		if (results[p1].score > 0) {
			myprintf("%-6d %-6d %-6d %-6d\n", results[p1].start, results[p1].len, results[p1].cnt, results[p1].score);
		}
	}

	myprintf("\nLosers...\n");
	for (int p1=0; p1<nSize-4; p1++) {
		if (results[p1].score < 0) {
			myprintf("%-6d %-6d %-6d %-6d\n", results[p1].start, results[p1].len, results[p1].cnt, results[p1].score);
		}
	}

	// try to calculate a resulting size. We still search for backrefs on anything that has a zero score
	// TODO: right now I'm just guesstimating size, the file isn't properly built
	int outSize = 0;
	for (int p1=0; p1<nSize-4; ) {
		if (results[p1].score > 0) {
			// this one is stored as a run
			if ((nLastrun != 0) && (OutputBuf[nLastrun] + results[p1].len < 127)) {
				// just add to the last run
				OutputBuf[nLastrun] += results[p1].len;
			} else {
				// new run
				nLastrun = outSize;
				OutputBuf[outSize++] = results[p1].len;
			}
			memcpy(&OutputBuf[outSize], &pSrc[results[p1].start], results[p1].len);
			outSize+=results[p1].len;	// for the data
			p1+=results[p1].len;
		} else {
			// need to search backwards
			int maxlen = 1;
			while ((maxlen+p1 < nSize-4) && (results[maxlen+p1].score <= 0) && (maxlen < 20)) ++maxlen;
//			if (results[maxlen+p1].score > 0) --maxlen;
			// search for a backref
			int worklen = maxlen;
			while (worklen > 4) {
				for (int p2=0; p2<p1-worklen; p2++) {
					if (0 == memcmp(&pSrc[p2], &pSrc[p1], worklen)) {
						// found it! store as a backref
						// TODO: need correct bitcode, and need to check for short backref
						OutputBuf[outSize++] = worklen | 0x80;
						OutputBuf[outSize++] = (p2>>8)&0xff;
						OutputBuf[outSize++] = p2&0xff;
						p1+=worklen;
						worklen = 0;
						nLastrun = 0;	// wasn't a run
						break;
					}
				}
				if (worklen) --worklen;
			}

			if (worklen > 0) {
				// can't find it, make a run
				// TOOD: the run might consolidate with the next block to save another byte 
				// - maybe a separate block consolidation pass
				if ((nLastrun != 0) && (OutputBuf[nLastrun] + maxlen < 127)) {
					// just add to the last run
					OutputBuf[nLastrun] += maxlen;
				} else {
					// new run
					nLastrun = outSize;
					OutputBuf[outSize++] = maxlen;
				}
				memcpy(&OutputBuf[outSize], &pSrc[results[p1].start], maxlen);
				outSize+=maxlen;	// for the data
				p1+=maxlen;
			}
		}
	}
	myprintf("Final output size approx %d bytes\n", outSize);


	printf("...\n");	// now we have an array of items scored to be worth keeping with look-ahead
}
#endif


int main(int argc, char* argv[])
{
	printf("v108 - 4/18/2020\n");

	if (argc < 2) {
		printf("vgmcomp [-v] [-scalefreq] [-halfvolrange] [-30hz] [-forcerun x] <filename>\n");
		printf(" -v - output verbose data\n");
		printf(" -scalefreq - apply frequency scaling if the frequency clock is not the NTSC clock\n");
		printf(" -halfvolrange - use 8 levels of volume instead of all 16 (lossy compression, usually sounds ok)\n");
		printf(" -30hz - code for 30hz instead of 60hz (may affect timing, will break arpeggio)\n");
		printf(" -forcerun x - force 'x' uncompressed bytes at the start of each stream (default %d, range 0-default)\n", MAX_RUN_SIZE);
		printf(" -lossyslide - use the old slide detection for lossy frequency packing, new system is popularity based\n");
		printf(" -packvolumes - volumes are packed two to a byte instead of one - requires packed player!\n");
		printf(" -minrun x - change the smallest runlength match tested (default %d)\n", minRunLoop);
		printf(" -maxrun x - change the largest runlength match tested (default %d)\n", maxRunLoop);
		printf(" -testout x - output song 'x' as a VGM for testing playback. use '0' if only one song was packed.\n");
		printf(" <filename> - VGM file to compress. Output will be <filename>.spf\n");
		return -1;
	}

	int arg=1;
	while ((arg < argc-1) && (argv[arg][0]=='-')) {
		if (0 == strcmp(argv[arg], "-v")) {
			verbose=true;
		} else if (0 == strcmp(argv[arg], "-scalefreq")) {
			scaleFreqClock=true;
		} else if (0 == strcmp(argv[arg], "-halfvolrange")) {
			halfvolrange=true;
		} else if (0 == strcmp(argv[arg], "-packvolumes")) {
			packVolumes=true;
		} else if (0 == strcmp(argv[arg], "-30hz")) {
			code30hz = true;
		} else if (0 == strcmp(argv[arg], "-lossyslide")) {
			lossyslide = true;
		} else if (0 == strcmp(argv[arg], "-forcerun")) {
			++arg;
			if (arg < argc-1) {
				forceRun = atoi(argv[arg]);
				if ((forceRun<0)||(forceRun > MAX_RUN_SIZE)) {
					printf("\rForcerun size from 0-%d\n", MAX_RUN_SIZE);
					return -1;
				}
			}
		} else if (0 == strcmp(argv[arg], "-testout")) {
			++arg;
			if (arg < argc-1) {
				nDesiredSong = atoi(argv[arg]);
			}
		} else if (0 == strcmp(argv[arg], "-minrun")) {
			++arg;
			if (arg < argc-1) {
				minRunLoop = atoi(argv[arg]);
				if (minRunLoop<3) {
					printf("\rminrun must be 3 or greater!\n");
					return -1;
				}
			}
		} else if (0 == strcmp(argv[arg], "-maxrun")) {
			++arg;
			if (arg < argc-1) {
				maxRunLoop = atoi(argv[arg]);
			}
		} else {
			printf("\rUnknown command '%s'\n", argv[arg]);
			return -1;
		}
		arg++;
	}
	if (maxRunLoop<minRunLoop) {
		printf("\rmaxrun must greater than minrun!\n");
		return -1;
	}

	int nMax=1;	// to prevent divide by zero warning

	while (arg+numSongs < argc) {
		FILE *fp=fopen(argv[arg+numSongs], "rb");
		if (NULL == fp) {
			printf("\rfailed to open file '%s'\n", argv[arg+numSongs]);
			return -1;
		}
		printf("\rReading %s - ", argv[arg+numSongs]);
		buffersize=fread(buffer, 1, sizeof(buffer), fp);
		nMax+=buffersize;
		fclose(fp);
		printf("%d bytes\n", buffersize);

		// -Split a VGM file into multiple channels (8 total - 4 audio, 3 tone and 1 noise)
		//  For the first pass, emit for every frame (that should be easier to parse down later)

		// The format starts with a 192 byte header:
		//
		//      00  01  02  03   04  05  06  07   08  09  0A  0B  0C  0D  0E  0F
		// 0x00 ["Vgm " ident   ][EoF offset     ][Version        ][SN76489 clock  ]
		// 0x10 [YM2413 clock   ][GD3 offset     ][Total # samples][Loop offset    ]
		// 0x20 [Loop # samples ][Rate           ][SN FB ][SNW][SF][YM2612 clock   ]
		// 0x30 [YM2151 clock   ][VGM data offset][Sega PCM clock ][SPCM Interface ]
		// 0x40 [RF5C68 clock   ][YM2203 clock   ][YM2608 clock   ][YM2610/B clock ]
		// 0x50 [YM3812 clock   ][YM3526 clock   ][Y8950 clock    ][YMF262 clock   ]
		// 0x60 [YMF278B clock  ][YMF271 clock   ][YMZ280B clock  ][RF5C164 clock  ]
		// 0x70 [PWM clock      ][AY8910 clock   ][AYT][AY Flags  ][VM] *** [LB][LM]
		// 0x80 [GB DMG clock   ][NES APU clock  ][MultiPCM clock ][uPD7759 clock  ]
		// 0x90 [OKIM6258 clock ][OF][KF][CF] *** [OKIM6295 clock ][K051649 clock  ]
		// 0xA0 [K054539 clock  ][HuC6280 clock  ][C140 clock     ][K053260 clock  ]
		// 0xB0 [Pokey clock    ][QSound clock   ] *** *** *** ***  *** *** *** ***
	
		// 56 67 6D 20 0B 1F 00 00 01 01 00 00 94 9E 36 00 
		// 00 00 00 00 C7 1D 00 00 91 D4 12 00 40 02 00 00 
		// 00 3A 11 00 3C 00 00 00 00 00 00 00 00 00 00 00 
		// 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
		// 50 80 50 00 50 A0 50 30 50 C0 50 00 50 E4 50 92 
		// 50 B3 50 D5 50 F0 62 50 A0 50 00 50 CF 50 1A 50 
		// E4 50 94 50 B4 50 F4 62 62 50 C9 50 1B 50 92 50 
		// B3 50 D6 50 F5 62 62 50 C3 50 1C 50 94 50 D7 50 
		// F7 62 62 50 C0 50 00 50 95 50 B4 50 D5 50 F4 62 
		// 62 50 96 50 BF 50 D6 50 F5 62 62 50 D7 50 F4 62 
		// 62 50 D8 50 FF 62 62 50 CF 50 1A 50 D5 50 F4 62 
		// 62 50 C9 50 1B 50 D6 50 F5 62 62 50 C3 50 1C 50

		// verify VGM
		if (0x206d6756 != *((unsigned int*)buffer)) {
			int cnt = tryvgz(); // returns difference between bufferlen and new data
			if (0x206d6756 != *((unsigned int*)buffer)) {
				printf("\rFailed to find VGM tag.\n");
				return -1;
			}
			// update nMax
			nMax+=cnt;
		}

		unsigned int nEOF=4+*((unsigned int*)&buffer[4]);
		if (nEOF > buffersize) {
			printf("\rWarning: EOF in header past end of file! Truncating.\n");
			nEOF = buffersize;
		}

		unsigned int nVersion = *((unsigned int*)&buffer[8]);
		myprintf("Reading version 0x%X\n", nVersion);

		unsigned int nClock = *((unsigned int*)&buffer[12]);
		nClock&=0x0FFFFFFF;
		double nNoiseRatio = 1.0;
		if ((nClock < 3579545-10) || (nClock > 3579545+10)) {
			freqClockScale = 3579545.0/nClock;
			printf("\rUnusual clock rate %dHz. Scale factor %f.\n", nClock, freqClockScale);
		}
		unsigned int nRate = 60;
		if (nVersion > 0x100) {
			nRate = *((unsigned int*)&buffer[0x24]);
            if (nRate == 0) {
                printf("\rRefresh rate set to 0, treating as 60\n");
                nRate = 60;
            }
			if ((nRate!=50)&&(nRate!=60)) {
				printf("\rweird refresh rate %d\n", nRate);
				return -1;
			}
		}
		myprintf("Refresh rate %d Hz\n", nRate);
		unsigned int nShiftRegister=16;
		if (nVersion >= 0x110) {
			nShiftRegister = buffer[0x2a];
			if ((nShiftRegister!=16)&&(nShiftRegister!=15)) {
				printf("\rweird shift register %d, treating as 15\n", nShiftRegister);
                nShiftRegister = 15;
			}
		}
		// TI/Coleco has a 15 bit shift register, so if it's 16 (default), scale it down
		if (nShiftRegister == 16) {
			nNoiseRatio *= 15.0/16.0;
			myprintf("Selecting 16-bit shift register.\n");
		}

		unsigned int nOffset=0x40;
		if (nVersion >= 0x150) {
			nOffset=*((unsigned int*)&buffer[0x34])+0x34;
			if (nOffset==0x34) nOffset=0x40;		// if it was 0
		}

		for (int idx=0; idx<8; idx+=2) {
			nCurrentTone[idx]=1;		// highest pitch except for noise
			nCurrentTone[idx+1]=15;		// mute volume
		}
		nCurrentTone[6]=0x10001;		// set noise trigger flag
		nTicks[numSongs] = 0;
		int nCmd=0;
		bool delaywarn = false;			// warn about imprecise delay conversion

		// Use nRate - if it's 50, add one tick delay every 5 frames
		// If user-defined noise is used, multiply voice 3 frequency by nNoiseRatio
		// Use nOffset for the pointer
		// Stop parsing at nEOF
		// 1 'sample' is intended to be at 44.1kHz
		while (nOffset < nEOF) {
			static int nRunningOffset = 0;

            // parse data for a tick
			switch (buffer[nOffset]) {		// what is it? what is it?
			case 0x50:		// PSG data byte
				{
					// here's the good stuff!
					unsigned char c = buffer[nOffset+1];

					if (c&0x80) {
						// it's a command byte - update the byte data
						nCmd=(c&0x70)>>4;
						c&=0x0f;

						// save off the data into the appropriate stream
						switch (nCmd) {
						case 1:		// vol0
						case 3:		// vol1
						case 5:		// vol2
						case 7:		// vol3
							// single byte (nibble) values
							nCurrentTone[nCmd]=c;
							break;

						case 6:		// noise
							// single byte (nibble) values, masked to indicate a retrigger
							nCurrentTone[nCmd]=c|0x10000;
							break;

						default:	
							// tone command, least significant nibble
							nCurrentTone[nCmd]&=0x3f0;
							nCurrentTone[nCmd]|=c;
							break;
						}
					} else {
						// non-command byte, use the previous command and update
						nCurrentTone[nCmd]&=0x00f;
						nCurrentTone[nCmd]|=(c&0x3f)<<4;
					}
				}
				nOffset+=2;
				break;

			case 0x61:		// 16-bit wait value
				{
					unsigned int nTmp=buffer[nOffset+1] | (buffer[nOffset+2]<<8);
					// divide down from samples to ticks (either 735 for 60hz or 882 for 50hz)
					if (nTmp % ((nRate==60)?735:882)) {
						if ((nRunningOffset == 0) && (!delaywarn)) {
							printf("\rWarning: Delay time loses precision (total %d, remainder %d samples).\n", nTmp, nTmp % ((nRate==60)?735:882));
							delaywarn=true;
						}
					}
					{
						// this is a crude way to do it - but if the VGM is consistent in its usage, it works
						// (ie: Space Harrier Main BGM uses this for a faster playback rate, converts nicely)
						int x = (nTmp+nRunningOffset)%((nRate==60)?735:882);
						nTmp=(nTmp+nRunningOffset)/((nRate==60)?735:882);
						nRunningOffset = x;
					}
					while (nTmp > 0) {
						for (int idx=0; idx<8; idx++) {
							if (nCurrentTone[idx] == 0) {
								if (((idx&1)==0) && (idx!=6)) {
									nCurrentTone[idx]=1;	// remap 0 to as close to mute as we have (tone only)
								}
							}

							VGMStream[idx+numSongs*8][nTicks[numSongs]]=nCurrentTone[idx];
							nCurrentTone[idx]&=0xffff;
						}
						if ((nRate==50)&&(nTicks[numSongs]%6==0)) {
							// output an extra frame delay
							for (int idx=0; idx<8; idx++) {
								if (nCurrentTone[idx] == 0) {
									if (((idx&1)==0) && (idx!=6)) {
										nCurrentTone[idx]=1;	// remap 0 to as close to mute as we have (tone only)
									}
								}

								VGMStream[idx+numSongs*8][nTicks[numSongs]]=nCurrentTone[idx];
								nCurrentTone[idx]&=0xffff;
							}
						}
						nTmp--;
						nTicks[numSongs]++;
						if (nTicks[numSongs] > maxTicks) maxTicks = nTicks[numSongs];
						if (nTicks[numSongs] > MAXTICKS) {
							printf("\rtoo many ticks (%d), can not process. Need a shorter song.\n", MAXTICKS);
							return -1;
						}
					}
					nOffset+=3;
				}
				break;

			case 0x62:		// wait 735 samples (60th second)
			case 0x63:		// wait 882 samples (50th second)
				// going to treat both of these the same. My output intends to run at 60Hz
				// and so this counts as a tick
				for (int idx=0; idx<8; idx++) {
					if (nCurrentTone[idx] == 0) {
						if (((idx&1)==0) && (idx!=6)) {
							nCurrentTone[idx]=1;	// remap 0 to as close to mute as we have (tone only)
						}
					}

					VGMStream[idx+numSongs*8][nTicks[numSongs]]=nCurrentTone[idx];
					nCurrentTone[idx]&=0xffff;
				}
				if ((nRate==50)&&(nTicks[numSongs]%6==0)) {
					// output an extra frame delay
					for (int idx=0; idx<3; idx++) {
						if (nCurrentTone[idx] == 0) {
							if (((idx&1)==0) && (idx!=6)) {
								nCurrentTone[idx]=1;	// remap 0 to as close to mute as we have (tone only)
							}
						}

						VGMStream[idx+numSongs*8][nTicks[numSongs]]=nCurrentTone[idx];
						nCurrentTone[idx]&=0xffff;
					}
				}
				nTicks[numSongs]++;
				if (nTicks[numSongs] > maxTicks) maxTicks = nTicks[numSongs];
				if (nTicks[numSongs] > MAXTICKS) {
					printf("\rtoo many ticks (%d), can not process. Need a shorter song.\n", MAXTICKS);
					return -1;
				}
				nOffset++;
				break;

			case 0x66:		// end of data
				nOffset=nEOF+1;
				break;

			case 0x70:		// wait 1 sample
			case 0x71:		// wait 2 samples
			case 0x72:
			case 0x73:
			case 0x74:
			case 0x75:
			case 0x76:
			case 0x77:
			case 0x78:
			case 0x79:
			case 0x7a:
			case 0x7b:
			case 0x7c:
			case 0x7d:
			case 0x7e:
			case 0x7f:		// wait 16 samples
				// try the same hack as above
				if (nRunningOffset == 0) {
					printf("\rWarning: fine timing lost.\n");
				}
				nRunningOffset+=buffer[nOffset]-0x70;
				if (nRunningOffset > ((nRate==60)?735:882)) {
					nRunningOffset -= ((nRate==60)?735:882);
				
					for (int idx=0; idx<8; idx++) {
						if (nCurrentTone[idx] == 0) {
							if (((idx&1)==0) && (idx!=6)) {
								nCurrentTone[idx]=1;	// remap 0 to as close to mute as we have (tone only)
							}
						}

						VGMStream[idx+numSongs*8][nTicks[numSongs]]=nCurrentTone[idx];
						nCurrentTone[idx]&=0xffff;
					}
					if ((nRate==50)&&(nTicks[numSongs]%6==0)) {
						// output an extra frame delay
						for (int idx=0; idx<8; idx++) {
							if (nCurrentTone[idx] == 0) {
								if (((idx&1)==0) && (idx!=6)) {
									nCurrentTone[idx]=1;	// remap 0 to as close to mute as we have (tone only)
								}
							}

							VGMStream[idx+numSongs*8][nTicks[numSongs]]=nCurrentTone[idx];
							nCurrentTone[idx]&=0xffff;
						}
					}
					nTicks[numSongs]++;
					if (nTicks[numSongs] > maxTicks) maxTicks = nTicks[numSongs];
					if (nTicks[numSongs] > MAXTICKS) {
						printf("\rtoo many ticks (%d), can not process. Need a shorter song.\n", MAXTICKS);
						return -1;
					}
				}
				nOffset++;
				break;

			// skipped opcodes - entered only as I encounter them - there are whole ranges I /could/ add, but I want to know.
			case 0x4f:		// game gear stereo (ignore)
				nOffset+=2;
				break;

			// unsupported sound chips
			case 0x51:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM2413 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x52:
			case 0x53:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM2612 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x54:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM2151 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x55:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM2203 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x56:
			case 0x57:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM2608 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x58:
			case 0x59:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM2610 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x5A:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM3812 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x5B:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM3526 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x5C:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YM8950 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x5D:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YMZ280B skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0x5E:
			case 0x5F:
				{
					static bool warn = false;
					if (!warn) {
						printf("\rUnsupported chip YMF262 skipped\n");
						warn = true;
					}
					nOffset+=3;
				}
				break;

			case 0xff:		// reserved, skip 4 bytes
				nOffset+=5;
				break;

			default:
				printf("\rUnsupported command byte 0x%02X at offset 0x%04X\n", buffer[nOffset], nOffset);
				return -1;
			}
		}

		myprintf("File %d parsed! Processed %d ticks (%f seconds)\n", numSongs+1, nTicks[numSongs], (float)nTicks[numSongs]/60.0);
		undeadStream3 = false;

		if (nShiftRegister != 15) {
			int cnt=0;
			myprintf("Adapting channel 3 for user-defined shift rates...");
			// go through every frame -- if the noise channel is user defined,
			// adapt the tone by nRatio
			for (int idx=0; idx<nTicks[numSongs]; idx++) {
				if (((VGMStream[6+numSongs*8][idx]&0x0f)==3)||((VGMStream[6+numSongs*8][idx]&0x0f)==7)) {
					if (VGMStream[5+numSongs*8][idx] != 0x0f) {
						// we can hear it! Don't tune it! But warn the user
						static int warned = false;
						if (!warned) {
							if (VGMStream[7+numSongs*8][idx] != 0xf) {
								warned=true;
								printf("\nSong employs user-tuned noise channel, but both noise and voice are audible.\n");
								printf("Not tuning the audible voice, but this may cause noise to be detuned. Use\n");
								printf("a 15-bit noise channel if your tracker supports it!\n");
							}
						}
					} else {
						VGMStream[4+numSongs*8][idx]=(int)((VGMStream[4+numSongs*8][idx]/nNoiseRatio)+0.5);		// this is verified on hardware!
						undeadStream3 = true;
						cnt++;
					}
				}
			}
			myprintf("%d notes tuned.\nChannel 3 will be preserved even if silent.\n", cnt);
		} else {
			// we don't need to tune, but we DO need to check whether channel 3 might be undead.
			int cnt=0;
			myprintf("Checking if channel 3 is tuning noises...");
			// go through every frame -- if the noise channel is user defined, then we are set
			for (int idx=0; idx<nTicks[numSongs]; idx++) {
				if (((VGMStream[6+numSongs*8][idx]&0x0f)==3)||((VGMStream[6+numSongs*8][idx]&0x0f)==7)) {
					undeadStream3 = true;
					myprintf("Tuning detected, channel 3 will be preserved even if silent.\n", cnt);
					break;
				}
			}
		}

		if (scaleFreqClock) {
			int clip = 0;
			// debug is emitted earlier - a programmable sound chip rate, like the F18A gives,
			// could playback at the correct frequencies, otherwise we'll just live with the
			// detune. The only one I have is 680 Rock (which /I/ made) and it's 2:1
			if (freqClockScale != 1.0) {
				myprintf("Adapting tones for unusual clock rate... ");
				// scale every tone - clip at the minimum (which is >0000 on TI, so automatic)
				for (int idx=0; idx<nTicks[numSongs]; idx++) {
					VGMStream[0+numSongs*8][idx] *= freqClockScale;
					if (VGMStream[0+numSongs*8][idx] == 0) clip++;
					if (VGMStream[0+numSongs*8][idx] > 0x3ff) { VGMStream[0+numSongs*8][idx]=0x3ff; clip++; }

					VGMStream[2+numSongs*8][idx] *= freqClockScale;
					if (VGMStream[2+numSongs*8][idx] == 0) clip++;
					if (VGMStream[2+numSongs*8][idx] > 0x3ff) { VGMStream[2+numSongs*8][idx]=0x3ff; clip++; }

					VGMStream[4+numSongs*8][idx] *= freqClockScale;
					if (VGMStream[4+numSongs*8][idx] == 0) clip++;
					if (VGMStream[4+numSongs*8][idx] > 0x3ff) { VGMStream[4+numSongs*8][idx]=0x3ff; clip++; }
				}
				if (clip > 0) myprintf("%d tones clipped");
				myprintf("\n");
			}
		}

		if (code30hz) {
			// cut the number of entries in half - the savings are obvious, but the output of
			// the song can be damaged, too. (Even with compression, at least the time streams
			// are halved). Arpeggios will be completely destroyed. But this may be useful
			// for being able to split processing between music and sound effects on separate ticks
			printf("\rReducing to 30hz playback (lossy)....\n");
			for (int chan=0; chan<8; chan++) {
				// noise trigger MUST be preserved, so check for that
				for (int idx=0; idx<nTicks[numSongs]; idx+=2) {
					// to try and improve arpeggios, check the two words and take the first
					// one that's different from current. But always take retrigger flags
					// This works acceptably well.
					if ((idx==0)||(VGMStream[chan+numSongs*8][idx/2-1]!=VGMStream[chan+numSongs*8][idx])) {
						VGMStream[chan+numSongs*8][idx/2]=VGMStream[chan+numSongs*8][idx] | (VGMStream[chan+numSongs*8][idx+1] & 0xffff0000);
					} else {
						VGMStream[chan+numSongs*8][idx/2]=VGMStream[chan+numSongs*8][idx+1] | (VGMStream[chan+numSongs*8][idx] & 0xffff0000);
					}
				}
			}
			nTicks[numSongs]/=2;
		}

		numSongs++;
	}

#if 0
	//as a quick test, re-output the file to see if we captured it correctly
	{
		int nDesiredSong = 0;		// desired song to test (must exist!)
		unsigned char out[]={
			0x56,0x67,0x6D,0x20,0x0B,0x1F,0x00,0x00,0x10,0x01,0x00,0x00,0x94,0x9E,0x36,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x91,0xD4,0x12,0x00,0x40,0x02,0x00,0x00,
			0x00,0x3A,0x11,0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
		};
		*((unsigned int*)&out[4])=nTicks[nDesiredSong]*11-4;		// set length (not actually correct!)
		*((unsigned int*)&out[0x18])=nTicks[nDesiredSong];			// set number of waits (this should be right!)
		FILE *fp=fopen("C:\\new\\test.vgm", "wb");
		fwrite(out, 64, 1, fp);
		for (int idx=0; idx<nTicks[nDesiredSong]; idx++) {
			for (int i2=nDesiredSong*8; i2<(nDesiredSong*8)+8; i2++) {
				// skip unchanged notes - mask is to detect noise triggers without outputting noises twice
				if ((idx>0)&&(VGMStream[i2][idx] == (VGMStream[i2][idx-1]&0xffff))) continue;

				fputc(0x50,fp);

				switch (i2) {
				case 1:
				case 3:
				case 5:
				case 6:
				case 7:
					// single nibble (strip any flags)
					fputc((VGMStream[i2][idx]&0xf) | (i2<<4) | 0x80, fp);
					break;

				default:
					// dual byte
					fputc((VGMStream[i2][idx]&0xf) | (i2<<4) | 0x80, fp);
					fputc(0x50, fp);
					fputc((VGMStream[i2][idx] & 0x3f0)>>4, fp);
					break;
				}
			}
			// all channels out, delay 1/60th second
			fputc(0x62, fp);
			if (code30hz) fputc(0x62,fp);	// second delay for 30hz
		}
		// output end of data
		fputc(0x66, fp);
		fclose(fp);
	}
	return 1;
#endif
#if 0
	// output a few seconds as an artrag-format raw file for testing the voice playback
	// Each frame is 3 words
	// Each word is a channel packed in this way
	// XXVV VVPP PPPP PPPP
	// XX = 00 always, 11 if the frame has ended
	// VVVV = volume in the SN76489 format
	// PPPPPPPPPP = period of the tone in 1-1023, where 1 = 111860Hz to 1023 = 109Hz
	{
		FILE *fp=fopen("C:\\new\\test.art", "wb");
		for (int idx=0; idx<nTicks[0]; idx++) {
			if (idx>=300) {
				break;
			}
			// we output EVERY sample, except noise related
			// stream 0,1 - 2,3 - 4,5 (tone, vol)
			int outword;
			outword = (VGMStream[0][idx]&0x3ff) | ((VGMStream[1][idx]&0x0f)<<10);
			fputc(outword>>8, fp);
			fputc(outword&0xff, fp);
			outword = (VGMStream[2][idx]&0x3ff) | ((VGMStream[3][idx]&0x0f)<<10);
			fputc(outword>>8, fp);
			fputc(outword&0xff, fp);
			outword = (VGMStream[4][idx]&0x3ff) | ((VGMStream[5][idx]&0x0f)<<10);
			fputc(outword>>8, fp);
			fputc(outword&0xff, fp);
		}
		// output end of data
		// this will stop it ;)
		fputc(0xff, fp);
		fputc(0xff, fp);
		fputc(0xff, fp);
		fputc(0xff, fp);
		fputc(0xff, fp);
		fputc(0xff, fp);
		fclose(fp);
		exit(1);
	}
#endif



	// remap the frequencies into a lookup table - this only helps if we can do it with just
	// 256 or fewer actual frequencies (which may be unlikely with sweeps, but let's see)
	// For now - just count how many frequencies there are. Note that we don't use a table
	// for the noise channel.
	int nTotalFreqs=260;
	int FreqCompress = 1;	// minimum difference for frequencies
	while (nTotalFreqs > 256) {
		// TODO: the reduction doesn't take unimportant frequencies into account (ie: muted channels)
		// if we add this, make sure not to screw up the custom noises generated by tone channel 3,
		// even if it's muted!
		myprintf("Counting number of frequencies...");
		nTotalFreqs=0;
		for (int song=0; song<numSongs*8; song+=8) {
			for (int idx=0; idx<nTicks[song/8]; idx++) {
				for (int chan=0; chan<5; chan+=2) {
					int cnt;
					for (cnt=0; cnt<nTotalFreqs; cnt++) {
						if (VGMStream[chan+song][idx] == Freqs[cnt]) break;
					}
					if (cnt >= nTotalFreqs) {
						Freqs[nTotalFreqs++] = VGMStream[chan+song][idx];
						if (nTotalFreqs >= 65535) {
							printf("\rToo many frequencies to process tune (>64k!)\n");
							return -1;
						}
					}
				}
			}
		}
		myprintf("%d frequencies used.\n", nTotalFreqs);

		if (nTotalFreqs > 256) {
			if (lossyslide) {
				// we can only fit 256 frequencies in the lookup table, so lossily reduce the scale of slides
				// retune to notes when that is closer
				// testing on my library says this is very rarely needed (and never on the retune so far)
				++FreqCompress;
				printf("\nLossy compression of frequencies with step %d...", FreqCompress);

				int changed=0, tuned=0;
				for (int song=0; song<numSongs*8; song+=8) {
					for (int chan=0; chan<5; chan+=2) {
						int lastFreq = VGMStream[chan+song][0];

						for (int idx=1; idx<nTicks[song/8]; idx++) {
							// find the closest musical note to teh current frequency, to decide which way to tune it
							int closest = 0, closestrange=9999;
							for (int i2=0; i2<sizeof(musicalnotes)/sizeof(musicalnotes[0]); i2++) {
								if (ABS(musicalnotes[i2]-VGMStream[chan+song][idx]) < closestrange) {
									closest=musicalnotes[i2];
									closestrange=ABS(musicalnotes[i2]-closest);
								}
							}

							if (ABS(lastFreq - VGMStream[chan+song][idx]) >= FreqCompress) {
								// this diff is okay
								lastFreq = VGMStream[chan+song][idx];
							} else {
								++changed;
								if (ABS(lastFreq - VGMStream[chan+song][idx]) <= ABS(closest - VGMStream[chan+song][idx])) {
									VGMStream[chan+song][idx] = lastFreq;
								} else {
									// closer to a musical note - use that instead
									VGMStream[chan+song][idx] = closest;
									lastFreq=closest;
									++tuned;
								}
							}
						}
					}
				}
				myprintf("%d notes changed (%d tuned).\n", changed, tuned);
			} else {
				// do note removal by popularity instead of by stepping slides
				// actual musical notes get their popularity tripled automatically
				// this seems to work better, although I worry it could completely destroy 
				// sides in a song with just one of them... much better on Monty though!
				int pop[1024];		// only 1024 possible frequencies
				memset(pop, 0, sizeof(pop));

				++FreqCompress;

				for (int song=0; song<numSongs*8; song+=8) {
					for (int chan=0; chan<5; chan+=2) {
						for (int idx=1; idx<nTicks[song/8]; idx++) {
							int freq = VGMStream[chan+song][idx];
							++pop[freq];
						}
					}
				}

				// tripling popularity of real notes
				for (int i2=0; i2<sizeof(musicalnotes)/sizeof(musicalnotes[0]); i2++) {
					pop[i2]*=3;
				}

				// go through the song and replace all the weakest notes
				int cnt = 0;

				// TODO: maybe we should leave channel 3 alone? We could screw up the bassline of user-defined noise...
				for (int song=0; song<numSongs*8; song+=8) {
					for (int chan=0; chan<5; chan+=2) {
						for (int idx=1; idx<nTicks[song/8]; idx++) {
							if (pop[VGMStream[chan+song][idx]] < FreqCompress) {
								// make it a rounded multiple of step (this sometimes reintroduces notes previously removed...)
								int replace = ((VGMStream[chan+song][idx] + (FreqCompress/2)) / FreqCompress) * FreqCompress;
								VGMStream[chan+song][idx] = replace;
								++cnt;
							}
						}
					}
				}
				myprintf("-Popularity removed %d notes at step %d\n", cnt, FreqCompress);
			}
		}
	}

	// now, remap all the streams to be single-byte indexes into the frequency table
	myprintf("Remapping notes to indexes....\n");
	for (int song=0; song<numSongs*8; song+=8) {
		for (int idx=0; idx<nTicks[song/8]; idx++) {
			for (int chan=0; chan<5; chan+=2) {
				// there should be an exact match for every note
				int x;
				for (x=0; x<nTotalFreqs; x++) {
					if (Freqs[x] == VGMStream[chan+song][idx]) {
						VGMStream[chan+song][idx] = x;
						break;
					}
				}
				if (x >= nTotalFreqs) {
					// should never happen!
					printf("\rCouldn't find frequency in table!\n");
				}
			}
		}
	}

	if (halfvolrange) {
		// cut the number of volumes in half - this can reduce the file size due to
		// fewer steps in volume slides :)
		printf("\rReducing volume resolution (lossy)....\n");
		for (int song=0; song<numSongs*8; song+=8) {
			for (int idx=0; idx<nTicks[song/8]; idx++) {
				for (int chan=1; chan<8; chan+=2) {
					VGMStream[chan+song][idx]|=0x01;		// making it odd ensures that we always have mute available ;)
				}
			}
		}
	}

	if (packVolumes) {
		// pack the volume bytes two to a byte (instead of one)
		// this requires a player that can handle it
		printf("\rPacking volumes....\n");
		for (int song=0; song<numSongs*8; song+=8) {
			// truncate last cycle if it's odd
			if (nTicks[song/8]&1) nTicks[song/8]--;
			for (int idx=0; idx<nTicks[song/8]; idx+=2) {
				for (int chan=1; chan<8; chan+=2) {
					int n1 = VGMStream[chan+song][idx]&0x0f;
					int n2 = VGMStream[chan+song][idx+1]&0x0f;
					VGMStream[chan+song][idx] = (n1<<4)|n2;
					VGMStream[chan+song][idx+1] = VGMStream[chan+song][idx];
				}
			}
		}
	}

#if 0
	// change the volume streams into deltas, starting with a volume of 0x0F
	// The whole byte is available, so we can delta from -15 to +15 and reach
	// the whole range
	// not used: intuitively, this sounds good! But in practice it's MUCH, MUCH worse
	myprintf("Mapping volume deltas....\n");
	for (int song=0; song<numSongs*8; song+=8) {
		for (int chan=1; chan<8; chan+=2) {
			int nOldVol=15;
			for (int idx=0; idx<nTicks[song/8]; idx++) {
				int nNewVol=VGMStream[chan+song][idx];
				VGMStream[chan+song][idx]=nNewVol-nOldVol;
				nOldVol=nNewVol;
			}
		}
	}
#endif

	// now we are ready to attempt the run-length. In addition to the 8 streams of data,
	// we have 4 streams of timing information. (per song)
	myprintf("Packing...\n");
	
	for (int song=0; song<numSongs*8; song+=8) {
		for (int idx=0; idx<8; idx++) {
			OutPos[idx+song]=0;
			if (idx < 4) {
				TimePos[idx+song/2]=0; // song/8*4
			}
		}
	}

	// each byte of timestream indicates:
	// bit  0x80 -	load one sample (lookup table index for tone channels)
	// bit  0x40 -	load one byte volume
	// bits 0x3f -	delay this many frames
	// special cases for volume only patterns 0x7A-0x7F - they represent specific strings, see below
	int nTotal=0;
	for (int song=0; song<numSongs*8; song+=8) {
		int nRunCount[4]={0,0,0,0};
		unsigned char nBits[4]={0xc0,0xc0,0xc0,0xc0};		// for the first update - output volume and frequency!

		for (int idx=0; idx<8; idx+=2) {
			nCurrentTone[idx]=0x10400;	// illegal first note in the song, and set the new noise bit
			nCurrentTone[idx+1]=15;		// mute volume
		}

		for (int idx=0; idx<=nTicks[song/8]; idx++) {
			for (int idx2=0; idx2<8; idx2+=2) {
				// we force output of the last tick so that it can be a sound mute in the song, and so the last run is output otherwise! 
				if ((idx != nTicks[song/8]) && ((nCurrentTone[idx2]&0xffff) == VGMStream[idx2+song][idx]) && (nCurrentTone[idx2+1] == VGMStream[idx2+1+song][idx])) {		// check tone and volume together
					// it's part of a run - no further change
					nRunCount[idx2/2]++;
					bool bDone = false;
					if (nBits[idx2/2] == 0x40) {
						// for volume only, the last 6 values have special meaning
						if (nRunCount[idx2/2] == 0x39) {
							bDone = true;
						}
					} else { 
						if (nRunCount[idx2/2] == 0x3f) {
							bDone = true;
						}
					}
					if (bDone) {
						// this is the maximum run size - emit it and reset
						if (nBits[idx2/2]&0x80) {
							if (idx2!=6) {
								// this is a tone channel (8-bit index)
								OutStream[idx2+song][OutPos[idx2+song]++]=(VGMStream[idx2+song][idx]&0x00ff);
							} else {
								// noise channel (strip flags - they are just used to force retrigger when needed)
								OutStream[idx2+song][OutPos[idx2+song]++]=(VGMStream[idx2+song][idx]&0x000f);
							}
						}
						// volume stream
						if (nBits[idx2/2] & 0x40) {
							OutStream[idx2+1+song][OutPos[idx2+1+song]++]=(VGMStream[idx2+1+song][idx]&0x000f);
						}
						if (nRunCount[idx2/2] == 0) {
							// this shouldn't happen, just verify my assumption!
							printf("\rRuncount of zero - packing bug!\n");
							return 1;
						}
						nBits[idx2/2]|=nRunCount[idx2/2];
						TimeStream[idx2/2+song/2][TimePos[idx2/2+song/2]++]=nBits[idx2/2];
						nRunCount[idx2/2]=0;
						nBits[idx2/2] = 0;		// we know no more changes need to be output for this run
					}
				} else {
					// new data or last tick - emit old data and prepare
					if (nRunCount[idx2/2] > 0) {
						if (nBits[idx2/2] & 0x80) {
							if (idx2!=6) {
								// this is a tone channel (8-bit index)
								OutStream[idx2+song][OutPos[idx2+song]++]=(VGMStream[idx2+song][idx-1]&0x00ff);
							} else {
								// noise channel
								OutStream[idx2+song][OutPos[idx2+song]++]=(VGMStream[idx2+song][idx-1]&0x000f);
							}
						}
						// volume stream
						if (nBits[idx2/2] & 0x40) {
							OutStream[idx2+1+song][OutPos[idx2+1+song]++]=(VGMStream[idx2+1+song][idx-1]&0x000f);
						}
						if (nRunCount[idx2/2] == 0) {
							// this shouldn't happen, just verify my assumption!
							printf("\rRuncount of zero - packing bug!\n");
							return 1;
						}
						nBits[idx2/2]|=nRunCount[idx2/2];
						TimeStream[idx2/2+song/2][TimePos[idx2/2+song/2]++]=nBits[idx2/2];
					}

					// determine the new nBits - what has changed?
					nBits[idx2/2] = 0;
					// detect different or noise trigger
					if (((nCurrentTone[idx2]&0xffff) != VGMStream[idx2+song][idx])) {
						nBits[idx2/2]|=0x80;	// frequency
					}
					if (nCurrentTone[idx2+1] != VGMStream[idx2+1+song][idx]) {
						nBits[idx2/2]|=0x40;	// volume
					}

					nRunCount[idx2/2]=1;
					//nRunStart[idx2/2]=idx-1;

					nCurrentTone[idx2]=VGMStream[idx2+song][idx];
					nCurrentTone[idx2+1]=VGMStream[idx2+1+song][idx];
				}
			}
		}

		// if stream 3 is undead, then drop a mute into its volume stream
		// if there ARE notes and there are NO volume changes AND we flagged undead
		if ((OutPos[4+song]!=0)&&(OutPos[4+1+song]==0)&&(undeadStream3)) {
			myprintf("Tagging channel 3 for noise tuning.\n");
			OutStream[4+1+song][OutPos[4+1+song]++]=0x0f;
			TimeStream[2+song/2][0] |= 0x40;	// volume change (should already be a note there)
		}

		// flag the end of each stream
		for (int idx=0; idx<4; idx++) {
			TimeStream[idx+song/2][TimePos[idx+song/2]++]=0;
		}
		myprintf("Done song %d, output streams:\n", (song/8)+1);
		for (int idx=0; idx<8; idx+=2) {
			myprintf("%d - %d tone, %d volume, %d timestamps\n", idx/2+song/2, OutPos[idx+song], OutPos[idx+1+song], TimePos[idx/2+song/2]);
			nTotal+=OutPos[idx+song]+OutPos[idx+1+song]+TimePos[idx/2+song/2];
			if ((OutPos[idx+song]==0)||(OutPos[idx+1+song]==0)) {
				deadStream[idx/2+song/2]=true;
			} else {
				deadStream[idx/2+song/2]=false;
			}
		}
	}
	myprintf("-- %d bytes\n", nTotal);

#if 0
	//as a quick test, re-output the file to see if we packed it correctly
	// TODO: packvolumes is not supported
	{
		unsigned char out[]={
			0x56,0x67,0x6D,0x20,0x0B,0x1F,0x00,0x00,0x10,0x01,0x00,0x00,0x94,0x9E,0x36,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x91,0xD4,0x12,0x00,0x40,0x02,0x00,0x00,
			0x00,0x3A,0x11,0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
		};
		int nDesiredSong=1;											// which song to export - MUST exist!
		*((unsigned int*)&out[4])=nTicks[nDesiredSong]*11-4;		// set length (incorrectly, but whatever)
		*((unsigned int*)&out[0x18])=nTicks[nDesiredSong];			// set number of waits (this should be right!)
		int cpos[8], tpos[4], tcnt[4];
		int oldNoise=-1;
		for (int idx=0; idx<8; idx++) {
			cpos[idx]=0;
			if (idx<4) {
				tpos[idx]=0;
				tcnt[idx]=1;
			}
		}

		FILE *fp=fopen("C:\\new\\test.vgm", "wb");
		fwrite(out, 64, 1, fp);
		for (int idx=0; idx<nTicks[nDesiredSong]; idx++) {
			for (int i2=0; i2<4; i2++) {
				tcnt[i2]--;
				if (tcnt[i2]<=0) {
					int tmp=TimeStream[i2+nDesiredSong*4][tpos[i2]++];
					if (tmp&0x80) {
						// output tone data
						if (i2 < 3) {
							// tone data - lookup table
							int x=Freqs[OutStream[i2*2+nDesiredSong*8][cpos[i2*2]++]];
							fputc(0x50,fp);
							fputc((x&0x000f)|(i2<<5)|0x80, fp);
							fputc(0x50,fp);
							fputc((x&0xff0)>>4, fp);
						} else {
							fputc(0x50,fp);
							fputc((OutStream[i2*2+nDesiredSong*8][cpos[i2*2]++])|(i2<<5)|0x80, fp);
						}
					}
					if (tmp&0x40) {
						// output volume data
						fputc(0x50,fp);
						fputc(OutStream[i2*2+1+nDesiredSong*8][cpos[i2*2+1]++]|(i2<<5)|0x90, fp);
					}
					tcnt[i2]=tmp&0x3f;
				}
			}
			// all channels out, delay 1/60th second
			fputc(0x62, fp);
			if (code30hz) fputc(0x62,fp);	// second delay for 30hz
		}
		// output end of data
		fputc(0x66, fp);
		fclose(fp);
	}
	return 1;
#endif
#if 0
		fp=fopen("C:\\new\\testPre.bin", "wb");
		fwrite(OutStream[2], 1, OutPos[2], fp);
//		fwrite(TimeStream[2], 1, TimePos[2], fp);
		fclose(fp);
#endif

#if 1
	// this works REALLY well (saves multiple k on Axel F)
	// there are 6 special case codes for encoding timestreams, to represent how often volume changes tend to occur:
	// 0x7F -- run of four 0x41 bytes (AAAA)
	// 0x7E -- AAA
	// 0x7D -- AA
	// 0x7C -- BBB
	// 0x7B -- BB
	// 0x7A -- CC
	// Go through and remap those sequences now - they will need special casing in the player, but they are 
	// that common that it seems worth it
	for (int idx=0; idx<4*numSongs; idx++) {
		int start = TimePos[idx];
		for (int i2=0; i2<TimePos[idx]-4; i2++) {
			if (0 == memcmp(&TimeStream[idx][i2], "AAAA", 4)) {
				// match! replace it
				TimeStream[idx][i2]=0x7f;
				memmove(&TimeStream[idx][i2+1], &TimeStream[idx][i2+4], TimePos[idx]-i2-4);
				TimePos[idx]-=3;
				continue;
			}
			if (0 == memcmp(&TimeStream[idx][i2], "AAA", 3)) {
				// match! replace it
				TimeStream[idx][i2]=0x7e;
				memmove(&TimeStream[idx][i2+1], &TimeStream[idx][i2+3], TimePos[idx]-i2-3);
				TimePos[idx]-=2;
				continue;
			}
			if (0 == memcmp(&TimeStream[idx][i2], "AA", 2)) {
				// match! replace it
				TimeStream[idx][i2]=0x7d;
				memmove(&TimeStream[idx][i2+1], &TimeStream[idx][i2+2], TimePos[idx]-i2-2);
				TimePos[idx]-=1;
				continue;
			}
			if (0 == memcmp(&TimeStream[idx][i2], "BBB", 3)) {
				// match! replace it
				TimeStream[idx][i2]=0x7c;
				memmove(&TimeStream[idx][i2+1], &TimeStream[idx][i2+3], TimePos[idx]-i2-3);
				TimePos[idx]-=2;
				continue;
			}
			if (0 == memcmp(&TimeStream[idx][i2], "BB", 2)) {
				// match! replace it
				TimeStream[idx][i2]=0x7b;
				memmove(&TimeStream[idx][i2+1], &TimeStream[idx][i2+2], TimePos[idx]-i2-2);
				TimePos[idx]-=1;
				continue;
			}
			if (0 == memcmp(&TimeStream[idx][i2], "CC", 2)) {
				// match! replace it
				TimeStream[idx][i2]=0x7a;
				memmove(&TimeStream[idx][i2+1], &TimeStream[idx][i2+2], TimePos[idx]-i2-2);
				TimePos[idx]-=1;
				continue;
			}
		}
		myprintf("Timestream %d packed from %d bytes to %d bytes\n", idx, start, TimePos[idx]);
	}
#endif

	// that's the basic run-length encoding complete, now we need to string compress it
	// we do each channel independently, but we are allowed to look back into any previous
	// channel for patterns. All we need to do here is provide indexes to the start
	// point of each channel
	int nOutPos = 0;						// single stream
	myprintf("Using force runlength of %d\n", forceRun);
	// note the force runlength only applies to the first song -- if you want it to
	// apply to others, just create a new file. Since there is no header there
	// is no real advantage to packing songs that don't fit well together

	// compress the streams!
	int nOutPos0, nOutPos1;
	int PtrTable[12*MAXSONGS];						// one entry for each stream
	for (int bAllowCurSearch=0; bAllowCurSearch<2; bAllowCurSearch++) {
		// Mode 0 - never search inside the current block for back references
		// Mode 1 - always search inside the current block for back references
		// Mode 2 - test both per stream, take the best (now removed fully)
		// (Mode is printed +1, so 1-3, not 0-2)
		// Mode 2 is disabled because in all of my testing, it was NEVER better,
		// often much worse, and takes twice as long to search. Modes 0 and 1
		// were both best in some cases (usually mode 1 was overall). It's a bit
		// weird that 2 didn't take the best of both worlds, but it all comes
		// down to how the patterns lie in the end.

		myprintf("* Testing mode %d...\n", bAllowCurSearch+1);

		// there are 12 streams to compress per song - 4 tone, 4 volume, and 4 time - we just run them
		// one after the other, but there are 12 16-bit pointers at the end to index them, and one
		// 16-bit pointer at the beginning which points to that table
		// after the pointers comes the frequency table (a pointer at 0x0002 points to its offset)
		// the length of the table depends on the number of notes used, but has a maximum size
		// of 512 bytes
		nOutPos=4;

		memset(backrefCount, 0, sizeof(backrefCount));

		// off we go! all the streams each compressed into the pack
		for (int idx=0; idx<12*numSongs; idx++) {
			// set start of this stream
			PtrTable[idx]=nOutPos;
			int nStrStartPos=nOutPos;
			int nBestMinRun=4;
			int nBestMinRunSize=1024*1024;
			unsigned char *pStr;
			int nStrSize;

			if (deadStream[(idx%4)+(idx/12)*4]) {
				OutputBuf[nOutPos++] = 0;	// end of stream
				myprintf("Stream %d empty.\n", idx);
			} else {
				// try various minimum run sizes to see what gets the best comp
				// on the last one we just use whatever was best.
				// We give precedence to longer runs so later code can use them!
				for (int nLoop=maxRunLoop; nLoop>=minRunLoop; nLoop--) {		// changing the minimum results in worse results!
					int nMinRunSize=nLoop;
					if (nLoop == 3) {
						nMinRunSize=nBestMinRun;
					}
					nOutPos=nStrStartPos;

					// set a local pointer to the correct stream
					// compression loop is agnostic to the data.
					if ((idx%12)<4) {
						// tones and noise
						pStr=OutStream[(idx/12)*8+(idx%12)*2];
						nStrSize=OutPos[(idx/12)*8+(idx%12)*2];
					} else if ((idx%12)<8) {
						// volume
						pStr=OutStream[(idx/12)*8+((idx%12)-4)*2+1];
						nStrSize=OutPos[(idx/12)*8+((idx%12)-4)*2+1];
					} else {
						// time
						pStr=TimeStream[(idx/12)*4+(idx%12)-8];
						nStrSize=TimePos[(idx/12)*4+(idx%12)-8];
					}

#ifdef TESTPACK
					// experimental code - different search method
					if (nLoop == 20) TestPack(pStr, nStrSize);
#endif

					int nRunSize=0;
					int nRunStart=0;
					int nBestRefSize=0;
					int nBestRefStart=0;
					int nRepeatSize=0;		// how many times does the current character repeat?
					bool bBestIsRepeat = false;

					if (forceRun > 0) {
						if (forceRun > nStrSize) {
							//printf("\rWarning: truncating forced run for short stream.\n");
							OutputBuf[nOutPos++] = nStrSize;
						} else {
							OutputBuf[nOutPos++] = forceRun;
						}
						for (int idx=0; idx<forceRun; idx++) {
							if (idx >= nStrSize) break;
							OutputBuf[nOutPos++]=pStr[idx];
						}
					}


					for (int p=forceRun; p<nStrSize; ) {	// no incrementor!
						if (nOutPos > 64*1024) {
							break;
						}

						// decide whether to encode a run (of 1-63 bytes) or backwards point (4 bytes or more only)
						// search from the beginning for this byte, and then forward to check for a compressable run
						nBestRefSize=0;
						nBestRefStart=0;
						nRepeatSize=0;
						bBestIsRepeat = false;

						// check whether we might encode this as a repeated character (as a run, which is smaller than a backreference)
						if (pStr[p] == pStr[p+1]) {
							nRepeatSize=1;
							while ((p+nRepeatSize < nStrSize) && (nRepeatSize < MAX_RUN_SIZE) && (pStr[p] == pStr[p+nRepeatSize])) nRepeatSize++;
							// we found one! Does the repeat length help us? (back-refs are only useful for 4-chars or more)
							if ((nRepeatSize > 3) && (nRepeatSize > nBestRefSize)) {
								nBestRefSize = nRepeatSize;
								nBestRefStart = p;
								bBestIsRepeat = true;
							}
						}

						for (int s=4; s< (nRunSize!=0?nRunStart:nOutPos); s++) {	// can't include a current run as the length byte is not identified yet
							if (OutputBuf[s] == pStr[p]) {
								// we found one! (back-refs are only useful for 4-chars or more)
								// can we find a run length?
								int cnt=0;
								while ((p+cnt<nStrSize) && (s+cnt < (nRunSize!=0?nRunStart:nOutPos)) && (OutputBuf[s+cnt]==pStr[p+cnt]) && (cnt<MAX_RUN_SIZE)) cnt++;
								// one byte bonus for current stream, since it encodes smaller
								int oldbonus = ((nBestRefStart >= PtrTable[idx]) && (nBestRefStart < PtrTable[idx]+256)) ? 1:0;
								int newbonus = ((s >= PtrTable[idx]) && (s < PtrTable[idx]+256)) ? 1:0;
								if ((cnt>3)&&(cnt+newbonus > nBestRefSize+oldbonus)) {
									nBestRefSize=cnt;
									nBestRefStart=s;
									bBestIsRepeat = false;
								}
							}
						}

						if (bAllowCurSearch) {
							// Sometimes allowing references into the current block is good, sometimes it makes for worse
							// compression. The results when it's good are worth including it in the trials.
							// oddly, it only helps when it's all or nothing. Results PER STREAM are almost always worse
							if (nRunSize > 0) {
								for (int s=nRunStart+1; s<nOutPos; s++) {	// skip current run length byte - it's not assigned yet
									if (OutputBuf[s] == pStr[p]) {
										// we found one!
										// we found one! can we find a run length?
										int cnt=0;
										while ((p+cnt<nStrSize) && (s+cnt < nOutPos) && (OutputBuf[s+cnt]==pStr[p+cnt]) && (cnt<MAX_RUN_SIZE)) cnt++;
										if ((cnt>3)&&(cnt>nBestRefSize)) {
											nBestRefSize=cnt;
											nBestRefStart=s;
											bBestIsRepeat = false;
										}
									}
								}
							}
						}

						// finished searching, did we find a back reference?
						if ((nBestRefSize >= nMinRunSize) && (!bBestIsRepeat)) {
	#if 0
	// short back ref is 0-255
							// we did! so compose a back reference and update the pointers
							OutputBuf[nOutPos++]=0x80 + nBestRefSize;	// just 0x80 for backref

							// MSB first, and then we'll check for 0
							OutputBuf[nOutPos]=(nBestRefStart&0xff00)>>8;

							// check for short reference
							if (OutputBuf[nOutPos] != 0) {
								// long reference
								OutputBuf[nOutPos-1] |= 0x40;
								nOutPos++;
							}

							// if it's short, this write will overwrite the 0x00 byte
							OutputBuf[nOutPos++]=nBestRefStart&0xff;
	#else
							// short backref is within the same stream (this is very worth it)
							if ((nBestRefStart >= PtrTable[idx]) && (nBestRefStart < PtrTable[idx]+256)) {
								// short backref
								OutputBuf[nOutPos++]=0x80 + nBestRefSize;
								OutputBuf[nOutPos++]=(nBestRefStart-PtrTable[idx])&0xff;

								backrefCount[nBestRefStart]++;
							} else {
								// long backref
								OutputBuf[nOutPos++]=0xc0 + nBestRefSize;
								OutputBuf[nOutPos++]=(nBestRefStart&0xff00)>>8;
								OutputBuf[nOutPos++]=nBestRefStart&0xff;

								backrefCount[nBestRefStart]+=2;
							}
	#endif
							p+=nBestRefSize;

							// and before we loop, check if there was a run in progress to update
							if (nRunSize > 0) {
								OutputBuf[nRunStart]=nRunSize;
								nRunSize=0;
							}

							continue;
						}

						// we did not find a backreference, or repeat was better - check that
						if (nRepeatSize > 5) {		// it IS better to restrict this to longer runs! But exact value? Dunno... 5 works pretty well though
							// yeah, it's good, output it
							OutputBuf[nOutPos++] = 0x40 + nRepeatSize;		// 0x40 for repeated char
							OutputBuf[nOutPos++] = pStr[p];
							p+=nRepeatSize;

							// and before we loop, check if there was a run in progress to update
							if (nRunSize > 0) {
								OutputBuf[nRunStart]=nRunSize;
								nRunSize=0;
							}

							continue;
						}
					
						// so this will be a run of bytes
						if (nRunSize == 0) {
							nRunStart=nOutPos++;
							nRunSize=1;
						} else {
							if (nRunSize == MAX_RUN_SIZE) {		// I tested with smaller blocks (than 127), and this is better in all cases tested (not ALMOST all, but ALL)
								// this is as big as we allow, so restart it
								OutputBuf[nRunStart]=nRunSize;
								nRunSize=0;
								nRunStart=nOutPos++;
							}
							nRunSize++;
						}
						OutputBuf[nOutPos++]=pStr[p++];
					}
					// we're at the end, if there was a run in progress, update it's counter
					if (nRunSize > 0) {
						OutputBuf[nRunStart]=nRunSize;
					}

					// check this is one is better than the others
					if ((nOutPos-nStrStartPos) < nBestMinRunSize) {
						nBestMinRun=nMinRunSize;
						nBestMinRunSize=nOutPos-nStrStartPos;
					}

					if (nOutPos > 64*1024) {
						break;
					}
				}

				if (nOutPos > 64*1024) {
					printf("\r** output larger than 64k - can not continue.\n");
					break;
				}

				// that's the end of this stream - output some data
				if (nStrSize == 0) {
					printf("\rStream %d error - 0 length compression.\n", idx);
				} else {
					myprintf("Stream %d compressed from %d bytes to %d bytes (%d %%) with runlength %d\n", 
						idx, nStrSize, nOutPos-nStrStartPos, 100-((nOutPos-nStrStartPos)*100/nStrSize), nBestMinRun);
				}

			}
		}

		if (nOutPos < 64*1024) {
			// tables must be word aligned
			if (nOutPos&1) OutputBuf[nOutPos++]=0;

			// fill in the tables at the end
			// point to the table
			OutputBuf[0] = (nOutPos&0xff00)>>8;
			OutputBuf[1] = (nOutPos&0xff);

			// fill in the stream pointers
			for (int idx=0; idx<12*numSongs; idx++) {
				OutputBuf[nOutPos++] = (PtrTable[idx]&0xff00)>>8;
				OutputBuf[nOutPos++] = (PtrTable[idx]&0x00ff);
			}

			// copy in the frequency table - frequency table
			// is preshifted for the needed data - least significant
			// nibble in the first byte, and next two nibbles in
			// the second byte (same as would be written to the sound
			// chip!)
			// point to the table - should still be aligned!
			OutputBuf[2] = (nOutPos&0xff00)>>8;
			OutputBuf[3] = (nOutPos&0xff);
			for (int idx=0; idx<nTotalFreqs; idx++) {
				OutputBuf[nOutPos++] = (Freqs[idx]&0x00f);
				OutputBuf[nOutPos++] = (Freqs[idx]&0x0ff0)>>4;
			}
		}

		// save the results
		if (bAllowCurSearch==0) {
			memcpy(OutputBuf0, OutputBuf, nOutPos);
			nOutPos0=nOutPos;
		}
		if (bAllowCurSearch==1) {
			memcpy(OutputBuf1, OutputBuf, nOutPos);
			nOutPos1=nOutPos;
		}

		myprintf("Mode %d output size: %d bytes (%d %%)\n", bAllowCurSearch+1, nOutPos, 100-(nOutPos*100/nMax));

		// find the best backrefs
		// TODO: just totalling for now to see what the difference might be
		int b1,b2,b3,b4,b5,b6;
		b1=b2=b3=b4=b5=b6=0;
		for (int idx=0; idx<65536; idx++) {
			if ((backrefCount[idx]>2)&&(backrefCount[idx] > b1)) {
				b1=backrefCount[idx];
				backrefCount[idx]=0;
			}
			if ((backrefCount[idx]>2)&&(backrefCount[idx] > b2)) {
				b2=backrefCount[idx];
				backrefCount[idx]=0;
			}
			if ((backrefCount[idx]>2)&&(backrefCount[idx] > b3)) {
				b3=backrefCount[idx];
				backrefCount[idx]=0;
			}
			if ((backrefCount[idx]>2)&&(backrefCount[idx] > b4)) {
				b4=backrefCount[idx];
				backrefCount[idx]=0;
			}
			if ((backrefCount[idx]>2)&&(backrefCount[idx] > b5)) {
				b5=backrefCount[idx];
				backrefCount[idx]=0;
			}
			if ((backrefCount[idx]>2)&&(backrefCount[idx] > b6)) {
				b6=backrefCount[idx];
				backrefCount[idx]=0;
			}
		}
//		printf("Backref collapsing would save about %d bytes for a total of %d\n", b1+b2+b3+b4+b5+b6, nOutPos-b1-b2-b3-b4-b5-b6);
	}
	// which is better?
	if (nOutPos0 <= nOutPos) {
		if (nOutPos0 <= nOutPos1) {
			memcpy(OutputBuf, OutputBuf0, nOutPos0);
			nOutPos=nOutPos0;
			myprintf("* Selecting mode 1\n");
		} else if (nOutPos1 <= nOutPos) {
			memcpy(OutputBuf, OutputBuf1, nOutPos1);
			nOutPos=nOutPos1;
			myprintf("* Selecting mode 2\n");
		} else {
			myprintf("* Selecting mode 3\n");
		}
	} else if (nOutPos1 <= nOutPos) {
		memcpy(OutputBuf, OutputBuf1, nOutPos1);
		nOutPos=nOutPos1;
		myprintf("* Selecting mode 2\n");
	} else {
		myprintf("* Selecting mode 3\n");
	}

	printf("\rFinal output size: %d bytes (%d %%)\n", nOutPos, 100-(nOutPos*100/nMax));

#if 0
	// output stream pre and post compression so it can be compared
	{
		FILE *fp=fopen("C:\\new\\testPre.bin", "wb");
		fwrite(OutStream[6], 1, OutPos[6], fp);
		fwrite(TimeStream[2], 1, TimePos[2], fp);
		fclose(fp);

		fp=fopen("C:\\new\\testPost.bin", "wb");
		int nStart = 0;
		int nLen = nOutPos;
		fwrite(&OutputBuf[nStart], 1, nLen, fp);
		fclose(fp);
	}

	FILE *funcomp = fopen("C:\\new\\testpostonly.bin", "wb");
#endif

	
	//as a quick test, re-output the file to see if we packed it correctly
	// needs to be a while to allow breaks - NOT intended to loop!!
	while (nDesiredSong > -1) {
		FILE *fp;
		if (nDesiredSong >= numSongs) {
			printf("\rCAN'T OUTPUT SAMPLE SONG INDEX %d WHEN THERE ARE ONLY %d SONGS!\n", nDesiredSong, numSongs);
			break;
		}
		unsigned char out[]={
			0x56,0x67,0x6D,0x20,0x0B,0x1F,0x00,0x00,0x10,0x01,0x00,0x00,0x94,0x9E,0x36,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x91,0xD4,0x12,0x00,0x40,0x02,0x00,0x00,
			0x00,0x3A,0x11,0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
		};
		*((unsigned int*)&out[4])=nTicks[nDesiredSong]*11-4;			// set length (incorrectly, but whatever)
		*((unsigned int*)&out[0x18])=nTicks[nDesiredSong];				// set number of waits (this should be right!)
		
#if 1
		char nambuf[1024];
		strcpy(nambuf,".\\");
		char *p=strrchr(argv[arg], '\\');
		if (NULL == p) p=argv[arg]-1;
		strcat(nambuf,p+1);
		strcat(nambuf,".testout.vgm");
		printf("Writing test output file %s\n", nambuf);
		fp=fopen(nambuf, "wb");
#else
		fp=fopen("C:\\new\\test.vgm", "wb");
#endif
		fwrite(out, 64, 1, fp);

		// decompression of 12 streams takes a bit of memory
		// not too much code though
		int nStreamPos[12], nStreamRef[12], nStreamCnt[12]; // 16-bit ints minimum, means 72 bytes. Counts CAN be single byte, though
		int nTimeCnt[4];
		int tsCount[4], tsSpecial[4];						// magic handlers for the time stream special cases
		int volumecounter = 0;								// which volume nibble are we on?
		int ptr = OutputBuf[0]*256 + OutputBuf[1];			// pointer to the table
		ptr+=24*nDesiredSong;								// offset to the requested song (12 pointers, 2 bytes each, per song)
		for (int idx=0; idx<12; idx++) {
			nStreamPos[idx]=OutputBuf[ptr+idx*2]*256 + OutputBuf[ptr+idx*2+1];	// real position in file (if -1 on a timestream, the stream is finished)
			nStreamRef[idx]=0;											// backreference position, 0 if not in a backreference, 0xffff for repeated bytes
			nStreamCnt[idx]=0;											// bytes left to process in current run
			if (idx<4) nTimeCnt[idx]=0;									// number of ticks left before getting another timebyte
		}
		ptr = OutputBuf[2]*256 + OutputBuf[3];				// now it points to the frequency table

		// Streams:
		// 0-2	= tones
		// 3	= noise
		// 4-7	= volumes
		// 8-11	= time
		
#define GetCompressedByte(str,var)										\
	if ((nStreamRef[str] != 0) && (nStreamRef[str]!=0xffff)) {			\
		var=OutputBuf[nStreamRef[str]++];								\
		nStreamCnt[str]--;												\
		if (nStreamCnt[str]==0) nStreamRef[str]=0;						\
	} else {															\
		if (nStreamCnt[str] == 0) {										\
			nStreamCnt[str]=OutputBuf[nStreamPos[str]++];				\
			if (nStreamCnt[str]&0x80) {									\
				if (nStreamCnt[str]&0x40) {								\
					nStreamRef[str]=OutputBuf[nStreamPos[str]++]<<8;	\
					nStreamRef[str]|=OutputBuf[nStreamPos[str]++];		\
				} else {												\
					int offset = OutputBuf[0]*256 + OutputBuf[1];		\
					offset += 24 * nDesiredSong;						\
					offset = OutputBuf[offset+(str)*2]*256 + OutputBuf[offset+(str)*2+1]; \
					nStreamRef[str]=OutputBuf[nStreamPos[str]++]+offset;		\
				}														\
				nStreamCnt[str]&=0x3f;									\
				var=OutputBuf[nStreamRef[str]++];						\
				nStreamCnt[str]--;										\
			} else if (nStreamCnt[str]&0x40) {							\
				nStreamCnt[str]&=0x3f;									\
				nStreamCnt[str]--;										\
				nStreamRef[str]=0xffff;									\
				var=OutputBuf[nStreamPos[str]];							\
			} else if (nStreamCnt[str] == 0) {							\
				var = 0;												\
			} else {													\
				nStreamRef[str] = 0;									\
				var=OutputBuf[nStreamPos[str]++];						\
				nStreamCnt[str]--;										\
			}															\
		} else {														\
			var=OutputBuf[nStreamPos[str]];								\
			nStreamCnt[str]--;											\
			if ((nStreamRef[str]==0) || (nStreamCnt[str]==0)) {			\
				nStreamPos[str]++;										\
			}															\
		}																\
	}

		bool bDataLeft=true;
		tsCount[0]=0;
		tsCount[1]=0;
		tsCount[2]=0;
		tsCount[3]=0;
		while (bDataLeft) {
			bDataLeft=false;
			for (int idx=0; idx<4; idx++) {
				if (nStreamPos[8+idx]==-1) continue;		// this timestream is done
				bDataLeft=true;

				// count down this channel's timer
				if (nTimeCnt[idx] > 0) {
					nTimeCnt[idx]--;
					continue;
				}

				// unpack a byte from the timestream
				unsigned char ch;

				if (tsCount[idx] > 0) {
					ch=tsSpecial[idx];
					--tsCount[idx];
				} else {
					GetCompressedByte(8+idx, ch);
					if (ch == 0) {
						// end of the stream
						nStreamPos[8+idx]=-1;
						continue;
					}
					// check for special cases
					if ((ch > 0x79) && (ch < 0x80)) {
						// 0x7F -- run of four 0x41 bytes (AAAA)
						// 0x7E -- AAA
						// 0x7D -- AA
						// 0x7C -- BBB
						// 0x7B -- BB
						// 0x7A -- CC
						switch (ch) {
						case 0x7f:
						case 0x7e:
						case 0x7d:
							tsSpecial[idx]=0x41;
							tsCount[idx]=ch-0x7c;	// count is decremented to account for first use
							break;
						case 0x7c:
						case 0x7b:
							tsSpecial[idx]=0x42;
							tsCount[idx]=ch-0x7a;
							break;
						case 0x7a:
							tsSpecial[idx]=0x43;
							tsCount[idx]=1;
							break;
						}
						ch=tsSpecial[idx];
					}
				}

				// work out what to do with it!
				if (ch & 0x80) {
					// we need to output tone data
					unsigned char t;
					if (idx < 3) {
						// tone channel is an index into the frequency table
						GetCompressedByte(idx, t);
						fputc(0x50, fp);
						fputc(OutputBuf[ptr+(t*2)]|(idx<<5)|0x80, fp);
						fputc(0x50, fp);
						fputc(OutputBuf[ptr+(t*2)+1], fp);
					} else {
						// noise channel
						GetCompressedByte(idx, t);
						fputc(0x50, fp);
						fputc(t|(idx<<5)|0x80, fp);
					}
				}
				if (ch & 0x40) {
					// volume data
					static unsigned char t = 0;

					fputc(0x50, fp);

					if (packVolumes) {
						if (volumecounter == 0) {
							GetCompressedByte(4+idx, t);
							fputc((t>>4)|((idx*2)<<4)|0x90, fp);
							++volumecounter;
						} else {
							fputc((t&0x0f)|((idx*2)<<4)|0x90, fp);
							volumecounter = 0;
						}
					} else {
						GetCompressedByte(4+idx, t);
						fputc(t|((idx*2)<<4)|0x90, fp);
					}
				}

				// note how long to wait
				nTimeCnt[idx] = ch & 0x3f;
				nTimeCnt[idx]--;
			}

			// all channels out, delay 1/60th second
			fputc(0x62, fp);
			if (code30hz) fputc(0x62,fp);	// second delay for 30hz
		}
		// output end of data
		fputc(0x66, fp);
		fclose(fp);
		
		// never loop
		break;
	} 

	if (nOutPos >= 64*1024) {
		printf("\rAll modes resulted in files > 64k, unable to provide output.\n"); 
	} else {
		// output the real final binary file
		char name[256];
		strcpy(name, argv[arg]);
		strcat(name, ".spf");		// stream pack format? I dunno.
		FILE *fp=fopen(name, "wb");
		fwrite(OutputBuf, 1, nOutPos, fp);
		fclose(fp);
	}

	return 0;
}

