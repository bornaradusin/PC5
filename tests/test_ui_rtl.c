/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Real renderer checks: coordinates move, readable pixels never get reflected. */
#include "pc5_draw.h"
#include <stdio.h>
#include <string.h>
static int direction;
static int right_to_left(void) { return direction; }
#include "pc5_ui_rtl_draw.h"
static int checks,failed;
#define CHECK(v,msg) do { checks++; if(!(v)){fprintf(stderr,"RTL: %s\n",msg);failed++;} } while(0)
int main(int argc,char **argv)
{
 if(argc!=2)return 2;
 Pc5Fonts fonts={0};
 if(pc5_fonts_load(&fonts,argv[1]))return 2;
 Pc5Canvas a={pc5_alloc(800u*180u*4u),800,180,800,0,0},b={pc5_alloc(800u*180u*4u),800,180,800,0,0};
 if(!a.px||!b.px)return 2;
 direction=0;pc5_clear(&a,0xff000000u);pc5_round_rect(&a,30,10,80,30,0,0xffff0000u);
 CHECK(a.px[15*800+35]==0xffff0000u&&a.px[15*800+700]==0xff000000u,"LTR positions unchanged");
 direction=1;pc5_clear(&a,0xff000000u);pc5_round_rect(&a,30,10,80,30,0,0xffff0000u);
 CHECK(a.px[15*800+700]==0xffff0000u&&a.px[15*800+35]==0xff000000u,"Arabic button moves to the right");
 uint32_t pixels[2]={0xffff0000u,0xff0000ffu};Pc5Image image={2,1,pixels};
 pc5_clear(&a,0xff000000u);pc5_draw_image(&a,&image,30,10,20,10,256);
 CHECK((a.px[15*800+750]&0x00ffffffu)==0x00ff0000u&&(a.px[15*800+769]&0x00ffffffu)==0x000000ffu,"image relocates without flipping its red/blue content");
 const char *values[]={"32-bit","64-bit","Prince of Persia","/mnt/usb0/PC5/game.exe","العربية 32-bit"};
 for(size_t i=0;i<sizeof(values)/sizeof(values[0]);i++){
  pc5_clear(&a,0xff000000u);pc5_clear(&b,0xff000000u);
  pc5_text(&a,&fonts,PC5_FACE_BODY,30,60,0xffffffffu,values[i],600,0);
  (pc5_text)(&b,&fonts,PC5_FACE_BODY,770,60,0xffffffffu,values[i],600,2);
  CHECK(!memcmp(a.px,b.px,800u*180u*4u),"mirrored leading text remains a readable right-anchored run");
  pc5_clear(&a,0xff000000u);pc5_clear(&b,0xff000000u);
  pc5_text(&a,&fonts,PC5_FACE_BODY,760,60,0xffffffffu,values[i],600,2);
  (pc5_text)(&b,&fonts,PC5_FACE_BODY,40,60,0xffffffffu,values[i],600,3);
  CHECK(!memcmp(a.px,b.px,800u*180u*4u),"mirrored trailing values use a fixed left edge without bidi realignment");
 }
 pc5_clear(&a,0xff000000u);pc5_clear(&b,0xff000000u);
 pc5_text_wrap(&a,&fonts,PC5_FACE_BODY,30,50,300,3,36,0xffffffffu,"واجهة عربية مع اسم Prince of Persia ومسار /data/games",0);
 (pc5_text_wrap)(&b,&fonts,PC5_FACE_BODY,770,50,300,3,36,0xffffffffu,"واجهة عربية مع اسم Prince of Persia ومسار /data/games",2);
 CHECK(!memcmp(a.px,b.px,800u*180u*4u),"wrapped Arabic lines keep the same right edge");
 pc5_clear(&a,0xff000000u);pc5_clear(&b,0xff000000u);
 pc5_text_wrap(&a,&fonts,PC5_FACE_BODY,30,50,300,3,36,0xffffffffu,"واجهة عربية في صندوق مركزي",1);
 (pc5_text_wrap)(&b,&fonts,PC5_FACE_BODY,470,50,300,3,36,0xffffffffu,"واجهة عربية في صندوق مركزي",1);
 CHECK(!memcmp(a.px,b.px,800u*180u*4u),"centred wrapped boxes move as a whole");
 pc5_clear(&a,0xff000000u);pc5_clear(&b,0xff000000u);
 pc5_text(&a,&fonts,PC5_FACE_BODY,30,60,0xffffffffu,"\xe2\x80\xba",0,0);
 (pc5_text)(&b,&fonts,PC5_FACE_BODY,770,60,0xffffffffu,"\xe2\x80\xb9",0,2);
 CHECK(!memcmp(a.px,b.px,800u*180u*4u),"Arabic disclosure arrow points left without reflecting glyph pixels");
 pc5_clear(&a,0xff000000u);pc5_clear(&b,0xff000000u);
 int pc=pc5_text_width(&fonts,PC5_FACE_HEAD,"PC"),brand=pc+1+pc5_text_width(&fonts,PC5_FACE_HEAD,"5");
 int brand_x=800-30-brand;
 CHECK(rtl_wordmark(&a,&fonts,PC5_FACE_HEAD,30,60,0xffffffffu,0xff4d7cffu)==brand,"brand width stays unchanged");
 (pc5_text)(&b,&fonts,PC5_FACE_HEAD,brand_x,60,0xffffffffu,"PC",0,0);
 (pc5_text)(&b,&fonts,PC5_FACE_HEAD,brand_x+pc+1,60,0xff4d7cffu,"5",0,0);
 CHECK(!memcmp(a.px,b.px,800u*180u*4u),"two-colour PC5 brand never becomes 5PC");
 pc5_fonts_free(&fonts);pc5_free(a.px);pc5_free(b.px);
 printf("RTL layout renderer: %d checks, %d failed\n",checks,failed);
 return failed?1:0;
}
