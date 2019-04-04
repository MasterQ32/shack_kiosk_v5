#include "module.hpp"
#include "modules/screensaver.hpp"
#include "modules/mainmenu.hpp"
#include "modules/lightroom.hpp"
#include "modules/tramview.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <chrono>
#include <ctime>
#include <algorithm>

using namespace std::chrono;

SDL_Renderer * renderer;
SDL_Window * window;
SDL_Texture * home_icon;

static module * current_module;
static module * next_module;
static module * previous_module;

double total_time;
double time_step;

glm::ivec2 screen_size { 1280, 1024 };

std::filesystem::path resource_root;

void module::activate(module * other)
{
	next_module = other;
}

static SDL_Texture * splash_icon;

struct splash
{
	double progress = 0.0;
	SDL_Point center;
};

int main()
{
	resource_root = std::filesystem::current_path() / "resources";

	srand(time(nullptr));

	if(SDL_Init(SDL_INIT_EVERYTHING) < 0)
		die("Failed to initialize SDL: %s", SDL_GetError());
	atexit(SDL_Quit);

	if(IMG_Init(IMG_INIT_PNG) == 0)
		die("Failed to initialize SDL_image: %s", IMG_GetError());
	atexit(IMG_Quit);

	window = SDL_CreateWindow(
		"Kiosk v5.0",
		0, 0,
		1280, 1024,
		SDL_WINDOW_FULLSCREEN_DESKTOP
	);
	if(window == nullptr)
		die("Failed to create window: %s", SDL_GetError());

	renderer = SDL_CreateRenderer(
		window,
		-1, // best possible
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC
	);
	if(renderer == nullptr)
		die("Failed to create renderer: %s", SDL_GetError());

	{
		SDL_RendererInfo info;
		if(SDL_GetRendererInfo(renderer, &info) < 0)
			die("Failed to query renderer info: %s", SDL_GetError());
		fprintf(stdout,
			"Renderer: %s\n"
			"Max. Texture Size: %d*%d\n",
			info.name,
			info.max_texture_width,
			info.max_texture_height
		);
	}

	splash_icon = IMG_LoadTexture(renderer, (resource_root / "splash.png").c_str());
	if(splash_icon == nullptr)
		die("Failed to load splash.png: %s", SDL_GetError());
	SDL_SetTextureBlendMode(splash_icon, SDL_BLENDMODE_BLEND);

	home_icon = IMG_LoadTexture(renderer, (resource_root / "icons" / "home.png").c_str());
	if(home_icon == nullptr)
		die("Failed to load home.png: %s", SDL_GetError());

	// preload all resources
	module::get<screensaver>();
	module::get<mainmenu>();
	module::get<lightroom>();
	module::get<tramview>();

	// then activate screensaver as initial screen
	module::activate<screensaver>();

	std::vector<splash> splashes;

	SDL_Texture * frontbuffer = nullptr;
	SDL_Texture * backbuffer = nullptr;

	auto const recreate_rendertargets = [&]()
	{
		int dx, dy;
		SDL_GetWindowSize(window, &dx, &dy);

		if(frontbuffer != nullptr)
			SDL_DestroyTexture(frontbuffer);

		if(backbuffer != nullptr)
			SDL_DestroyTexture(backbuffer);

		frontbuffer = SDL_CreateTexture(
			renderer,
			SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_TARGET,
			dx, dy
		);
		if(frontbuffer == nullptr)
			die("Failed to create frontbuffer: %s", SDL_GetError());

		backbuffer = SDL_CreateTexture(
			renderer,
			SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_TARGET,
			dx, dy
		);
		if(backbuffer == nullptr)
			die("Failed to create backbuffer: %s", SDL_GetError());

		SDL_SetTextureBlendMode(frontbuffer, SDL_BLENDMODE_BLEND);
		SDL_SetTextureBlendMode(backbuffer, SDL_BLENDMODE_BLEND);
	};

	recreate_rendertargets();

	int pixel_transition_matrix[20][25];
	for(auto & row  : pixel_transition_matrix)
		for(auto & value : row)
			value = rand() % 64;

	int current_transition;
	double transition_progress;
	auto const next_transition = [&]()
	{
		current_transition = rand() % 3;
		transition_progress = 0.0;
	};
	next_transition();

	auto const startup = high_resolution_clock::now();
	auto last_frame = startup;
	auto last_event = startup;
	while(next_module != nullptr)
	{
		SDL_GetWindowSize(window, &screen_size.x, &screen_size.y);

		if(current_module != next_module)
		{
			if(current_module != nullptr)
				current_module->leave();
			previous_module = current_module;
			current_module = next_module;
			transition_progress = 0.0;
			if(current_module != nullptr)
				current_module->enter();
		}

		SDL_Event ev;
		while(SDL_PollEvent(&ev))
		{
			if((ev.type == SDL_MOUSEBUTTONDOWN) or (ev.type == SDL_KEYDOWN))
				last_event = high_resolution_clock::now();

			if(ev.type == SDL_MOUSEBUTTONDOWN)
			{
				auto & sp = splashes.emplace_back();
				sp.center.x = ev.button.x;
				sp.center.y = ev.button.y;
			}
			if(ev.type == SDL_QUIT)
				next_module = nullptr;
			else if(previous_module == nullptr) // if no transition is in progress
				current_module->notify(ev);
		}

		auto const now = high_resolution_clock::now();

		if((now - last_event) > seconds(15))
			module::activate<screensaver>();

		total_time = duration_cast<milliseconds>(now - startup).count() / 1000.0;
		time_step = duration_cast<milliseconds>(now - last_frame).count() / 1000.0;
		last_frame = now;

		for(auto & sp : splashes)
			sp.progress += time_step;

		// Render transition
		if(previous_module != nullptr)
		{
			SDL_SetRenderTarget(renderer, backbuffer);
			SDL_SetRenderDrawColor(renderer, 0x30, 0x30, 0x30, 0xFF);
			SDL_RenderClear(renderer);
			previous_module->render();

			SDL_SetRenderTarget(renderer, frontbuffer);
			SDL_SetRenderDrawColor(renderer, 0x30, 0x30, 0x30, 0xFF);
			SDL_RenderClear(renderer);
			current_module->render();

			SDL_SetRenderTarget(renderer, nullptr);
			SDL_SetRenderDrawColor(renderer, 0xFF, 0x00, 0xFF, 0xFF);
			SDL_RenderClear(renderer);

			switch(current_transition)
			{
				case 0: // alpha blend
				{
					SDL_RenderCopy(renderer, backbuffer, nullptr, nullptr);
					SDL_SetTextureAlphaMod(frontbuffer, std::clamp(255.0 * pow(transition_progress, 2.0), 0.0, 255.0));
					SDL_RenderCopy(renderer, frontbuffer, nullptr, nullptr);

					transition_progress += 5.0 * time_step;
					break;
				}

				case 1: // vertical sliding
				{
					int pos_y = screen_size.y * glm::smoothstep(0.0, 1.0, transition_progress);

					SDL_Rect position { 0, 0, screen_size.x, pos_y };
					SDL_RenderCopy(renderer, frontbuffer, &position, &position);

					position.y = position.h;
					position.h = screen_size.y - position.y;
					SDL_RenderCopy(renderer, backbuffer, &position, &position);

					position.y = pos_y - 2;
					position.h = 5;
					SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
					SDL_RenderFillRect(renderer, &position);

					transition_progress += 2.0 * time_step;
					break;
				}

				case 2: // pixels
				{
					int w = screen_size.x / 25;
					int h = screen_size.y / 20;

					int progress = 64 * transition_progress;

					for(int x = 0; x < 25; x++)
					{
						for(int y = 0; y < 20; y++)
						{
							SDL_Rect pos = { w * x, h * y, w, h };

							if(progress > pixel_transition_matrix[y][x])
								SDL_RenderCopy(renderer, frontbuffer, &pos, &pos);
							else
								SDL_RenderCopy(renderer, backbuffer, &pos, &pos);
						}
					}
					transition_progress += 4.5 * time_step;

					break;
				}
			}

			if(transition_progress >= 1.0)
			{
				next_transition();
				previous_module = nullptr;
			}
		}
		else
		{
			SDL_SetRenderTarget(renderer, frontbuffer);
			SDL_SetRenderDrawColor(renderer, 0x30, 0x30, 0x30, 0xFF);
			SDL_RenderClear(renderer);
			current_module->render();

			SDL_SetRenderTarget(renderer, nullptr);
			SDL_SetRenderDrawColor(renderer, 0xFF, 0x00, 0xFF, 0xFF);
			SDL_RenderClear(renderer);
			SDL_RenderCopy(renderer, frontbuffer, nullptr, nullptr);
		}

		for(auto const & splash : splashes)
		{
			int size = 100 * pow(splash.progress, 0.5);
			SDL_Rect rekt = {
				splash.center.x - size/2,
				splash.center.y - size/2,
				size, size
			};
			int x = std::max<int>(0, 255 - 255 * pow(splash.progress, 0.5));
			SDL_SetTextureAlphaMod(splash_icon, x);
			SDL_RenderCopy(
				renderer,
				splash_icon,
				nullptr,
				&rekt
			);
		}

		splashes.erase(
			std::remove_if(splashes.begin(), splashes.end(), [](splash const & s) { return s.progress >= 1.0; }),
			splashes.end()
		);

		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);

	return 0;
}







[[noreturn]] void __die(char const * file, int lineno, char const * msg, ...)
{
	va_list list;
	va_start(list, msg);

	fprintf(stderr, "%s:%d:", file, lineno);
	vfprintf(stderr, msg, list);
	fprintf(stderr, "\n");
	fflush(stderr);

	va_end(list);

	exit(1);
}
