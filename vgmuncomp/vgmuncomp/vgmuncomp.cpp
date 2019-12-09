// vgmuncomp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char OutputBuf[64*1024];		// 64k is maximum size due to pointers


int _tmain(int argc, _TCHAR* argv[])
{
	FILE *fp = NULL;
	char nambuf[1024] = "";
	bool code30hz = false;
	int nDesiredSong = 0;
	int numSongs;
	int nTicks = 0;

	if (argc > 1) {
		if (0 == strcmp(argv[1], "-30hz")) {
			code30hz = true;
			if (argc > 2) {
				strcpy(nambuf, argv[2]);
			}
			if (argc > 3) {
				nDesiredSong = atoi(argv[3]);
			}
		} else {
			strcpy(nambuf, argv[1]);
			if (argc > 2) {
				nDesiredSong = atoi(argv[2]);
			}
		}

		fp = fopen(nambuf, "rb");
	}
	if (NULL == fp) {
		printf("vgmuncomp [-30hz] <file.spf> [x] -> writes out a VGM version - optional 30hz or specify a track other than 0\n");
		return -1;
	}
	int nSize = fread(OutputBuf, 1, sizeof(OutputBuf), fp);
	if (!feof(fp)) {
		printf("Not EOF -- file larger than 64k?\n");
		return -1;
	}
	fclose(fp);

	numSongs = ((OutputBuf[2]*256 + OutputBuf[3]) - (OutputBuf[0]*256 + OutputBuf[1]))/24;
	printf("Opened file %s with %d songs.\n", nambuf, numSongs);
	printf("Extracting song index %d\n", nDesiredSong);
	if (code30hz) printf("Timing for 30hz playback\n");

	//as a quick test, re-output the file to see if we packed it correctly
	do {
		FILE *fp;
		if (nDesiredSong >= numSongs) {
			printf("\rCAN'T OUTPUT SAMPLE SONG INDEX %d WHEN THERE ARE ONLY %d SONGS! (0-%d)\n", nDesiredSong, numSongs, numSongs-1);
			break;
		}
		unsigned char out[]={
			0x56,0x67,0x6D,0x20,0x0B,0x1F,0x00,0x00,0x10,0x01,0x00,0x00,0x94,0x9E,0x36,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x91,0xD4,0x12,0x00,0x40,0x02,0x00,0x00,
			0x00,0x3A,0x11,0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
		};

		strcat(nambuf,".vgm");
		fp=fopen(nambuf, "wb+");
		if (NULL == fp) {
			printf("Failed to open output file\n");
			return 99;
		}

		fwrite(out, 64, 1, fp);

		// decompression of 12 streams takes a bit of memory
		// not too much code though
		int nStreamPos[12], nStreamRef[12], nStreamCnt[12]; // 16-bit ints minimum, means 72 bytes. Counts CAN be single byte, though
		int nTimeCnt[4];
		int tsCount[4], tsSpecial[4];						// magic handlers for the time stream special cases
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
					unsigned char t;
					GetCompressedByte(4+idx, t);
					fputc(0x50, fp);
					fputc(t|((idx*2)<<4)|0x90, fp);
				}

				// note how long to wait
				nTimeCnt[idx] = ch & 0x3f;
				nTimeCnt[idx]--;
			}

			// all channels out, delay 1/60th second
			fputc(0x62, fp);
			nTicks++;
			if (code30hz) {
				fputc(0x62,fp);	// second delay for 30hz
				nTicks++;
			}
		}
		// output end of data
		fputc(0x66, fp);

		// these matter to mod2psg, so get it right
		int p = ftell(fp);
		fseek(fp, 4, SEEK_SET);
		p-=4;
		fwrite(&p, 4, 1, fp);						// set length of data
		fseek(fp, 0x18, SEEK_SET);
		fwrite(&nTicks, 4, 1, fp);

		fclose(fp);
	} while (false);
//	fclose(funcomp);

	return 0;
}

