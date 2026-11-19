/* The TV's own screens the phone leaves out (its sign-in, its title page) are still named by shared code: here they
   stand for nothing (the phone's sign-in and title page are its own, in s_pages.c and s_title.c) */
#include "ui/gfx.h"
void login_backdrop_draw(void) {}
void login_glow_bake(void) {}
GLuint login_glow_tex(void) { return 0; }
void login_loading_draw(double since, float a) { (void)since; (void)a; }
void login_glows_draw(void) {}
int detail_page_btn_key(int k) { (void)k; return 0; }
