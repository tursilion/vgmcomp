// packer.cpp : Defines the entry point for the console application.
// This just pulls the stream compression code from vgmcomp and allows it
// to attempt to process any arbitrary stream of data.

#include <stdio.h>
#include <errno.h>
#include <string.h>

// ram is cheap ;)
unsigned char InputBuf[1024*1024];
unsigned char OutputBuf[513*1024];	// way more than we have room for
unsigned char OutputBuf0[512*1024];
unsigned char OutputBuf1[512*1024];
#define MAX_RUN_SIZE	63				// do not make larger than 63, other bits reserved

// at the moment, not making any effort at making it useful, just testing
// pStr - pointer to the buffer to compress
// nStrSize - length of the buffer to compress
// forceRun - number of bytes to start uncompressed (63 is the default - this is a crude 'dictionary')
int packstream(unsigned char *pStr, int nStrSize, int forceRun) {
	// that's the basic run-length encoding complete, now we need to string compress it
	// we do each channel independently, but we are allowed to look back into any previous
	// channel for patterns. All we need to do here is provide indexes to the start
	// point of each channel
	int nOutPos = 0;						// single stream
	printf("Using force runlength of %d\n", forceRun);
	// note the force runlength only applies to the first song -- if you want it to
	// apply to others, just create a new file. Since there is no header there
	// is no real advantage to packing songs that don't fit well together

	// compress the streams!
	int nOutPos0, nOutPos1;
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

		printf("* Testing mode %d...\n", bAllowCurSearch+1);

		// there's only one stream in this version of the code, so no indexes and no need to loop through them all
		// no pointers, either, just start with the data.
		nOutPos=0;

		int nStrStartPos=nOutPos;
		int nBestMinRun=4;
		int nBestMinRunSize=1024*1024;

		// try various minimum run sizes to see what gets the best comp
		// on the last one we just use whatever was best.
		// We give precedence to longer runs so later code can use them!
		for (int nLoop=20; nLoop>=3; nLoop--) {		// changing the minimum results in worse results!
			int nMinRunSize=nLoop;
			if (nLoop == 3) {
				nMinRunSize=nBestMinRun;
			}
			nOutPos=nStrStartPos;

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
				if (nOutPos > 512*1024) {
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
						int oldbonus = ((nBestRefStart >= 0) && (nBestRefStart < 256)) ? 1:0;
						int newbonus = ((s >= 0) && (s < 256)) ? 1:0;
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
					if ((nBestRefStart >= 0) && (nBestRefStart < 256)) {
						// short backref
						OutputBuf[nOutPos++]=0x80 + nBestRefSize;
						OutputBuf[nOutPos++]=(nBestRefStart)&0xff;
					} else {
						// long backref
						OutputBuf[nOutPos++]=0xc0 + nBestRefSize;
						OutputBuf[nOutPos++]=(nBestRefStart&0xff00)>>8;
						OutputBuf[nOutPos++]=nBestRefStart&0xff;
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

		if (nOutPos > 512*1024) {
			printf("\r** output larger than 512k - can not continue.\n");
			break;
		}

		// that's the end of this stream - output some data
		if (nStrSize == 0) {
			printf("\rStream error - 0 length compression.\n");
		} else {
			printf("Stream compressed from %d bytes to %d bytes (%d %%) with runlength %d\n", 
				nStrSize, nOutPos-nStrStartPos, 100-((nOutPos-nStrStartPos)*100/nStrSize), nBestMinRun);
		}

		if (nOutPos < 512*1024) {
			// word aligned
			if (nOutPos&1) OutputBuf[nOutPos++]=0;
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

		printf("Mode %d output size: %d bytes (%d %%)\n", bAllowCurSearch+1, nOutPos, 100-(nOutPos*100/nStrSize));
	}
	// which is better?
	if (nOutPos0 <= nOutPos) {
		if (nOutPos0 <= nOutPos1) {
			memcpy(OutputBuf, OutputBuf0, nOutPos0);
			nOutPos=nOutPos0;
			printf("* Selecting mode 1\n");
		} else if (nOutPos1 <= nOutPos) {
			memcpy(OutputBuf, OutputBuf1, nOutPos1);
			nOutPos=nOutPos1;
			printf("* Selecting mode 2\n");
		} else {
			printf("* Selecting mode 3\n");
		}
	} else if (nOutPos1 <= nOutPos) {
		memcpy(OutputBuf, OutputBuf1, nOutPos1);
		nOutPos=nOutPos1;
		printf("* Selecting mode 2\n");
	} else {
		printf("* Selecting mode 3\n");
	}

	printf("\rFinal output size: %d bytes (%d %%)\n", nOutPos, 100-(nOutPos*100/nStrSize));
	return nOutPos;
}

int main(int argc, char* argv[])
{
	if (argc < 3) {
		printf("packer <infile> <outfile>\n");
		return 0;
	}

	FILE *fp = fopen(argv[1], "rb");
	if (NULL == fp) {
		printf("Can't open input file - %d\n", errno);
		return 1;
	}
	int inSize = fread(InputBuf, 1, sizeof(InputBuf), fp);
	fclose(fp);
	printf("Read binary %d bytes\n", inSize);

	int outSize = packstream(InputBuf, inSize, 63);

	fp = fopen(argv[2], "wb");
	if (NULL == fp) {
		printf("Can't open output file - %d\n", errno);
		return 2;
	}
	fwrite(OutputBuf, 1, outSize, fp);
	fclose(fp);

	return 0;
}

