/*
 * creature-monitor - watching norns from afar?
 * Written starting in 2022 by 20kdc
 * To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide. This software is distributed without any warranty.
 * You should have received a copy of the CC0 Public Domain Dedication along with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "main.h"

#include <stdio.h>

extern const char cm_page_start[];
extern const int cm_page_len;

SDL_Window * gWindow;
SDL_Renderer * gRenderer;
SDL_Texture * gFont;

CMState * gCurrentState;

void errorOut(const char * reason) {
	puts(reason);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error!", reason, gWindow);
	exit(1);
}

void writeText(int x, int y, const char * text) {
	writeText(x, y, text, strlen(text));
}

void writeText(int x, int y, const CMSlice & textSlice) {
	writeText(x, y, textSlice.data, textSlice.length);
}

void writeText(int x, int y, const char * text, size_t len) {
	SDL_Rect src = {0, 0, 7, 14};
	SDL_Rect dst = {x, y, 7, 14};
	while (len) {
		len--;
		char ch = *text++;
		if (ch == 10) {
			dst.x = x;
			dst.y += 16;
		} else {
			src.x = (ch & 0x0F) * 7;
			src.y = ((ch & 0xF0) >> 4) * 14;
			SDL_RenderCopy(gRenderer, gFont, &src, &dst);
			dst.x += 8;
		}
	}
}

void fillRect(const SDL_Rect rect, uint32_t colour) {
	SDL_SetRenderDrawColor(gRenderer, (colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF, (colour >> 24) & 0xFF);
	SDL_RenderFillRect(gRenderer, &rect);
}

void setState(CMState * state) {
	if (gCurrentState != NULL)
		gCurrentState->queueDelete();
	printf("switching state to: %s\n", state->stateName());
	gCurrentState = state;
}

int main(int argc, char ** argv) {
	SDL_Init(SDL_INIT_VIDEO);
	SDLNet_Init();

	if (SDL_CreateWindowAndRenderer(640, 480, SDL_WINDOW_RESIZABLE, &gWindow, &gRenderer))
		errorOut("wah! failed to create window/renderer!");

	SDL_Surface * s = SDL_LoadBMP_RW(SDL_RWFromConstMem(cm_page_start, cm_page_len), 1);
	if (!s)
		errorOut("wah! failed to open BMP!");
	puts("opened BMP");

	gFont = SDL_CreateTextureFromSurface(gRenderer, s);
	if (!gFont)
		errorOut("wah! failed to create texture!");
	puts("created texture");

	setInitialState();
	while (1) {
		SDL_Delay(50);
		SDL_Event ev;
		int w, h;
		SDL_GetWindowSize(gWindow, &w, &h);
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT)
				return 0;
			gCurrentState->event(w, h, ev);
		}
		SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 255);
		SDL_RenderClear(gRenderer);
		gCurrentState->frame(w, h);
		SDL_RenderPresent(gRenderer);
		CMObject::performQueuedDeletions();
	}
}
