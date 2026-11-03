/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Launcher layout coordinates, scoped to pc5_ui.c's drawing section only.
 * Mirror placements, not image pixels, glyphs, QR modules or saved game art.
 * The CPU renderer itself and all other languages keep their old coordinates. */
#ifndef PC5_UI_RTL_DRAW_H
#define PC5_UI_RTL_DRAW_H
static inline float rtl_box(const Pc5Canvas *c, float x, float w) { return right_to_left() ? c->w - x - w : x; }
static inline float rtl_point(const Pc5Canvas *c, float x) { return right_to_left() ? c->w - x : x; }
static inline void rtl_rect(Pc5Canvas *c, float x, float y, float w, float h, float r, uint32_t color)
{ pc5_round_rect(c, rtl_box(c,x,w),y,w,h,r,color); }
static inline void rtl_frame(Pc5Canvas *c, float x, float y, float w, float h, float r, float line, uint32_t color)
{ pc5_round_frame(c,rtl_box(c,x,w),y,w,h,r,line,color); }
static inline void rtl_graded(Pc5Canvas *c,float x,float y,float w,float h,float r,float line,uint32_t a,uint32_t b)
{ pc5_round_frame_graded(c,rtl_box(c,x,w),y,w,h,r,line,a,b); }
static inline void rtl_circle(Pc5Canvas *c,float x,float y,float r,uint32_t color)
{ pc5_circle(c,rtl_point(c,x),y,r,color); }
static inline void rtl_ring(Pc5Canvas *c,float x,float y,float r,float line,uint32_t color)
{ pc5_ring(c,rtl_point(c,x),y,r,line,color); }
static inline void rtl_line(Pc5Canvas *c,float x,float y,float xx,float yy,float line,uint32_t color)
{ pc5_line(c,rtl_point(c,x),y,rtl_point(c,xx),yy,line,color); }
static inline void rtl_arc(Pc5Canvas *c,float x,float y,float r,float line,float a,float b,uint32_t color)
{ if(right_to_left()){float t=a;a=-b;b=-t;} pc5_arc(c,rtl_point(c,x),y,r,line,a,b,color); }
static inline void rtl_shadow(Pc5Canvas *c,float x,float y,float w,float h,float r,float spread,uint32_t color)
{ pc5_shadow(c,rtl_box(c,x,w),y,w,h,r,spread,color); }
static inline void rtl_image(Pc5Canvas *c,const Pc5Image *im,float x,float y,float w,float h,int alpha)
{ pc5_draw_image(c,im,rtl_box(c,x,w),y,w,h,alpha); }
static inline void rtl_cover(Pc5Canvas *c,const Pc5Picture *im,float x,float y,float w,float a,float b,float r,int bright,int alpha,float reflect)
{ if(right_to_left()){float t=a;a=b;b=t;} pc5_draw_cover(c,im,rtl_box(c,x,w),y,w,a,b,r,bright,alpha,reflect); }
static inline void rtl_card(Pc5Canvas *c,const Pc5Picture *im,float x,float y,float w,float a,float b,float r,int bright,int alpha,int reflect,float fade,int gloss)
{ if(right_to_left()){float t=a;a=b;b=t;} pc5_draw_card(c,im,rtl_box(c,x,w),y,w,a,b,r,bright,alpha,reflect,fade,gloss); }
static inline int rtl_text(Pc5Canvas *c,const Pc5Fonts *fonts,Pc5Face face,int x,int y,uint32_t color,const char *s,int max,int align)
{
 if(right_to_left()){
  x=c->w-x;align=align==0?2:align==2?3:align;
  /* A standalone disclosure arrow has no RTL text to trigger bidi mirroring. */
  if(!strcmp(s,"\xe2\x80\xba"))s="\xe2\x80\xb9";
 }
 return pc5_text(c,fonts,face,x,y,color,s,max,align);
}
static inline int rtl_wrap(Pc5Canvas *c,const Pc5Fonts *fonts,Pc5Face face,int x,int y,int w,int lines,int height,uint32_t color,const char *s,int align)
{
 if(right_to_left()){x=c->w-x-(align==1?w:0);align=align==0?2:align==2?3:align;}
 return pc5_text_wrap(c,fonts,face,x,y,w,lines,height,color,s,align);
}
/* The two-colour brand is one LTR object, not two independent RTL labels. */
static inline int rtl_wordmark(Pc5Canvas *c,const Pc5Fonts *fonts,Pc5Face face,int x,int y,uint32_t pc,uint32_t five)
{
 int first=pc5_text_width(fonts,face,"PC"),width=first+1+pc5_text_width(fonts,face,"5");
 x=(int)rtl_box(c,(float)x,(float)width);
 pc5_text(c,fonts,face,x,y,pc,"PC",0,0);
 pc5_text(c,fonts,face,x+first+1,y,five,"5",0,0);
 return width;
}
#define pc5_round_rect(...) rtl_rect(__VA_ARGS__)
#define pc5_round_frame(...) rtl_frame(__VA_ARGS__)
#define pc5_round_frame_graded(...) rtl_graded(__VA_ARGS__)
#define pc5_circle(...) rtl_circle(__VA_ARGS__)
#define pc5_ring(...) rtl_ring(__VA_ARGS__)
#define pc5_line(...) rtl_line(__VA_ARGS__)
#define pc5_arc(...) rtl_arc(__VA_ARGS__)
#define pc5_shadow(...) rtl_shadow(__VA_ARGS__)
#define pc5_draw_image(...) rtl_image(__VA_ARGS__)
#define pc5_draw_cover(...) rtl_cover(__VA_ARGS__)
#define pc5_draw_card(...) rtl_card(__VA_ARGS__)
#define pc5_text(...) rtl_text(__VA_ARGS__)
#define pc5_text_wrap(...) rtl_wrap(__VA_ARGS__)
#endif
