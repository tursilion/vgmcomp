This tool performs compression of VGM ('Video Game Music') files compatible with the TI sound chip (mostly the Sega Master System, Game Gear, and ColecoVision sources). It breaks down the playback into streams to increase redundancy, then packs using a combination of RLE and string compression. The resulting decompression is only a few lookups for each string to be unpacked, and does not require a decompression buffer (although it does use a fair bit of state data).

Features:
	-lossless compression of VGM files into a packed format
	-automatic handling of VGZ files (gzipped, even if the extension doesn't show it)
	-packing of multiple compressed VGMs into a single archive, with shared compression
	-adaptation of 50hz input into 60hz output
	-scaling of frequency clock to NTSC frequencies
	-optional lossy compression of volume levels
	-optional (slightly lossy) 30hz output mode
	-optional compression tuning for small sound files
	-full playback code for various scenarios

So what is it? A VGM file is a standard file that has been around for a while for recording music, originally from the Sega Master System, by saving the actual data written to the sound chip along with a timestamp. This allows for accurate playback of anything the sound chip can play, although the resulting file can sometimes be rather large.

So a VGM is essentially a recording of the data, as opposed to other formats like a tracker (which provides the instructions to playback notes with instruments and effects, such as MOD or MIDI), or a dedicated emulator which runs the original code (such as NSF or SID).

Because there is a large library of these songs, and because the data is compatible with our sound chip, and easy to generate, I've standardized on using it for my tools. This compressor was necessary to make use of the resulting files in a meaningful way.

Unlike traditional compressors, this code decompresses directly to the sound chip. Therefore, no decompression buffer is needed (and there is no latency!) However, it means that all string patterns are found in the /compressed/ data instead of the uncompressed data, causing some redundancy in the output file. Still, the resulting data compares reasonably well with gzipped copies of the VGM, approaching within single digit percentage points.

All you need to do to run it is type "vgmcomp" followed by the paths to the VGM files that you wish to pack together. It will perform an exhaustive search for compression patterns, and output the resulting file with the first filename you specified, appended by ".spf" (Sound Pack Format - never said I was creative). A minimum of one file and a maximum of 16 are allowed.

A few options exist for changing the result (options must be typed exactly as shown):

-v : verbose - outputs lots of information as it runs

-scalefreq : if the VGM does not use the NTSC clock for sound chip generation (VERY rare), scale all frequencies to that an NTSC clocked sound chip will play back correctly. If this IS used, you may find some resulting sounds are too low to be played, and will be dropped.

-halfvolrange : The sound chip has 16 levels of attenuation (volume), including mute. This option cuts it in half, to 8 levels, and may achieve better compression with a barely noticable change in audio quality. It is mostly useful on files with a lot of volume slides (used to simulate instruments), and probably won't have any effect on files containing only square wave music.

-30hz : the output file is normally intended to be played at 60hz. This option creates 30hz output, which can be used to reduce overall CPU load, or to interleave music and sound effect processing. Usually there is minimal effect, but songs that use a lot of arpeggio or other fast effects will sound different. The code attempts to preserve such fast effects, but they are still changing at 30hz instead of 60hz. This may also slightly improve compression.

-forcerun x : 'x' is a value from 0-63, default is 63. At the beginning of every stream, this many bytes from the original song are saved without string compression. This usually makes up for itself by providing a dictionary of uncompressed data for the rest of the stream. However, if you have very short songs, or the beginning of every stream is not characteristic of the later data, you can try a smaller number (including 0) and see if it compresses better. Note this is global, there is no way to specify per song today. 

-lossyslide : If there are more than 256 distinct frequencies in the song (or collection of songs), the compressor will reduce the number of notes available by replacing some. The default mechanism is popularity based, with musical notes having a boost. The old method was to try and detect pitch slides and step them more - this activates the old method

-testout x : 'x' is the index of the song you wish to output (0 for first) - the packed song will be unpacked and written as a vgm with ".testout.vgm" appended to the filename, so you can quickly listen to the result.

-minrun x : 'x' is the smallest runlength that will be tested for each stream. Default is 3.

-maxrun x : 'x' is the largest runlength that will be tested for each stream. Default is 20.

In practice, changing minrun and maxrun leads to longer searches with little benefit, but feel free to experiment.

Limitations:

-output is played at 60hz - faster sounds may be lost

-No more than 16 songs in a packed file

-Max 256 distinct frequencies in a packed file

-Maximum size of a packed file is 64k (but, the players require all data to be visible in memory at the same time, so 24k is a more practical limit, 8k for cart.)

-No 50hz playback mode - you can simulate 60hz with an extra call to the play function every 5th frame on PAL machines.

-See source files for memory requirements

Tips:

-If you have a need for different 'forcerun's, just create separate packed files (there is very little overhead in practice). For example, music might compress best with the default 63, while a pack file of short sound effects might take less space with 32 or 16. Experimentation is the only way to see what works best.

-Compression is single pass, which means that the search can only take into account what already exists. Although THAT search is fairly comprehensive, you might find better compression by changing the order of the files you are compressing. Again, only experimentation can answer this question. This was not done automatically because it would take a long search and make it exponentially longer, and because you can do it manually if you wish to try.

Other handy notes:

	format.txt - describes the packed data format and apologizes for it
	playback.txt - describes the playback functions
	PlaybackAsm994a - folder containing the Asm994a versions of the plaack code

