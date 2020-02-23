# 1 "quickplay.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 1 "quickplay.c"



# 1 "../../libti99/vdp.h" 1
# 27 "../../libti99/vdp.h"
inline void VDP_SET_ADDRESS(unsigned int x) { *((volatile unsigned char*)0x8C02)=((x)&0xff); *((volatile unsigned char*)0x8C02)=((x)>>8); }


inline void VDP_SET_ADDRESS_WRITE(unsigned int x) { *((volatile unsigned char*)0x8C02)=((x)&0xff); *((volatile unsigned char*)0x8C02)=(((x)>>8)|0x40); }


inline void VDP_SET_REGISTER(unsigned char r, unsigned char v) { *((volatile unsigned char*)0x8C02)=(v); *((volatile unsigned char*)0x8C02)=(0x80|(r)); }


inline int VDP_SCREEN_POS(unsigned int r, unsigned int c) { return (((r)<<5)+(c)); }


inline int VDP_SCREEN_TEXT(unsigned int r, unsigned int c) { return (((r)<<5)+((r)<<3)+(c)); }


inline int VDP_SCREEN_TEXT80(unsigned int r, unsigned int c) { return (((r)<<6)+((r)<<4)+(c)); }


inline int VDP_SCREEN_TEXT64(unsigned int r, unsigned int c) { return (((r)<<6)+(c)); }
# 172 "../../libti99/vdp.h"
int set_graphics_raw(int sprite_mode);

void set_graphics(int sprite_mode);





int set_text_raw();

void set_text();





int set_text80_raw();

void set_text80();







int set_text80_color_raw();

void set_text80_color();





void set_text64_color();







int set_multicolor_raw(int sprite_mode);

void set_multicolor(int sprite_mode);





int set_bitmap_raw(int sprite_mode);

void set_bitmap(int sprite_mode);




void writestring(int row, int col, char *pStr);



void vdpmemset(int pAddr, int ch, int cnt);



void vdpmemcpy(int pAddr, const unsigned char *pSrc, int cnt);



void vdpmemread(int pAddr, unsigned char *pDest, int cnt);





void vdpwriteinc(int pAddr, int nStart, int cnt);




extern void (*vdpchar)(int pAddr, int ch);
void vdpchar_default(int pAddr, int ch);




unsigned char vdpreadchar(int pAddr);



void vdpwritescreeninc(int pAddr, int nStart, int cnt);



void vdpscreenchar(int pAddr, int ch);



void vdpwaitvint();







int putchar(int x);







void putstring(char *s);



int puts(char *s);






int printf(char *str, ...);


void hexprint(unsigned char x);



void fast_hexprint(unsigned char x);



void faster_hexprint(unsigned char x);



void scrn_scroll_default();
extern void (*scrn_scroll)();


void fast_scrn_scroll();




void hchar(int r, int c, int ch, int cnt);




void vchar(int r, int c, int ch, int cnt);




unsigned char gchar(int r, int c);






void sprite(int n, int ch, int col, int r, int c);



void delsprite(int n);



void charset();




void charsetlc();
# 360 "../../libti99/vdp.h"
void gplvdp(int vect, int adr, int cnt);


void bm_setforeground(int c);


void bm_setbackground(int c);



void bm_clearscreen();




void bm_setpixel(unsigned int x, unsigned int y);




void bm_clearpixel(unsigned int x, unsigned int y);


void bm_drawline(int x0, int y0, int x1, int y1);







void bm_drawlinefast(int x0, int y0, int x1, int y1, int mode);



void bm_sethlinefast(unsigned int x0, unsigned int y0, unsigned int x1);


void bm_clearhlinefast(unsigned int x0, unsigned int y0, unsigned int x1);




void bm_consolefont();




void bm_putc(int c, int r, unsigned char alphanum);





void bm_puts(int c, int r, unsigned char* str);




void bm_placetile(int c, int r, const unsigned char* pattern);






extern unsigned char gBitmapColor;





extern unsigned char* gBmFont;




extern unsigned int gImage;
extern unsigned int gColor;
extern unsigned int gPattern;
extern unsigned int gSprite;
extern unsigned int gSpritePat;


extern int nTextRow,nTextEnd;
extern int nTextPos;

extern unsigned char gSaveIntCnt;


extern const unsigned int byte2hex[256];



void unlock_f18a();


void lock_f18a();
# 5 "quickplay.c" 2
# 1 "../../libti99/player.h" 1
# 20 "../../libti99/player.h"
extern volatile unsigned int * const pVoice;
extern volatile unsigned char * const pVol;
extern volatile unsigned char * const pDone;
# 35 "../../libti99/player.h"
void stinit(const void *pSong, const int index);


void ststop();




void stplay();




unsigned int stcount(const void *pSong);
# 6 "quickplay.c" 2




const unsigned char dummy='a';

const unsigned char textout[768+32] = {
    "~~~~DATAHERE~~~~\0"
};





int main() {

    set_graphics(0x00);
    vdpmemset(gImage, ' ', 768);
    charsetlc();



    const unsigned char *x = textout;
    for (int r=0; r<23; ++r) {
        unsigned char c= *(x++);
        if (c == 0) break;
        if (c > 31) c = 0;
        c = (32-c)/2;
        unsigned int adr = VDP_SCREEN_POS(r,c);
        VDP_SET_ADDRESS(adr);
        while (*x) {
            *((volatile unsigned char*)0x8C00) = *(x++);
        }
        ++x;
    }


    stinit((const void*)0xa000, 0);


    for (;;) {
        __asm__( "clr r12\n\ttb 2\n\tjeq -4\n\tmovb @>8802,r12" : : : "r12" );;
        __asm__( "bl @stplay\n\tlwpi >8300" );;

        __asm__("LIMI 2"); __asm__("LIMI 0");;
    }


    return 2;

}
