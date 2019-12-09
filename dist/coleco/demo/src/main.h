#ifndef byte
#define byte unsigned char
#endif

#define SPRITESCNT 7
struct cvu_sprite
{
	uint8_t y;
	uint8_t x;
	uint8_t name;
	uint8_t tag;
};

extern const cv_vmemp gIMAGE;
extern const cv_vmemp gPATTERN;
extern const cv_vmemp gCOLOR;
extern const cv_vmemp gSPRITES;
extern const cv_vmemp gSPRITE_PATTERNS;
extern volatile bool step;	
extern struct cvu_sprite sprites[SPRITESCNT];
extern struct cv_controller_state cs;

