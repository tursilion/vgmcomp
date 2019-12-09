The playback libraries were written with the intention of being called from a GCC compiled program, and so assume the GCC runtime. These files have been adapted very minimally from GCC syntax to something that Asm994A (Cory Burr's assembler) will accept, so you can use them from your assembly programs.

Fortunately, the interface is straight forward, as GCC passes values by register when it can. Simply load the first parameter in R1, the second in R2, and the third in R3 (as needed). Return values, when any, return in R1.

The code uses a workspace at "songwp" (>8322 by default), and a define easily changes this. The code also assumes that your calling workspace is >8300, and a few places set back to this (as well as reading parameters from there.) If you don't wish to use >8300 in your calling app, you may want to search for all instances of ">83" and upate them (It is not going to find all the cases to search for ">8300", there are direct references to R1, R2 and R3 in that workspace).

The code will need more work to assemble under Editor/Assembler. You will have to make the text uppercase (at least the code), ensure that all lines are reduced to under 80 characters, replace tabs with spaces (I think), and perhaps most tricky, ensure all labels are 6 characters or less.

The main difference of this code is the removal of the 'dseg' and 'pseg' directives (they are left in as comments). If you wish to separate the code and data, you can search for these terms and replace with AORG or the directive of your choice. 

Fair warning: I've not tested this version of the code. But since it's the same code as GCC is compiling, I assume it works. Do let me know! ;)
