// any fire button
#define CV_FIRE (CV_FIRE_0 | CV_FIRE_1 | CV_FIRE_2 | CV_FIRE_3)
// helper macros
#define set_sprite_x(ii,ix) sprites[ii].x=ix;
#define set_sprite_y(ii,iy) sprites[ii].y=iy;
#define set_sprite_col(ii,ic) sprites[ii].tag=ic;
#define set_sprite_chr(ii,ic) sprites[ii].name=(ic)<<2;	/* 4 chars per large sprite */
#define get_sprite_chr(ii) (sprites[ii].name>>2)

// for some reason this lib treats 0 attenuation as mute, AND 30 attenuation as mute.
// so you can't have no attenuation.
#define VOL_MIN 28
#define VOL_MAX 2	
#define VOL_OFF 30

extern volatile bool step;

void nmi();
unsigned int rand();
int abs(int x);

