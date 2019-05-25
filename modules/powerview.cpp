#include "powerview.hpp"
#include "http_client.hpp"
#include "widgets/button.hpp"
#include "protected_value.hpp"
#include "rendering.hpp"

#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>
#include <atomic>

namespace /* static */
{
	std::atomic_flag fast_reload;
	std::atomic_int scroll_progress;

	int zoom_level = 2;

	struct zoomscale
	{
		int value;
		char const * display;
	};

	zoomscale const zoom_scale[] =
	{
	  {   15, "30 sec" },
	  {   30,  "1 min" },
	  {   60,  "2 min" },
	  {  150,  "5 min" },
	  {  300, "10 min" },
	  {  900, "30 min" },
	  { 1800, "60 min" },
	  { 3600,  "2 std" },
	  { 9000,  "5 std" },
	};
	size_t const zoom_scale_cnt = sizeof(zoom_scale) / sizeof(*zoom_scale);

	struct powernode
	{
		double time;
		double phase[3];

		double total() const { return phase[0] + phase[1] + phase[2]; }
	};

	protected_value<std::vector<powernode>> nodes;

	[[noreturn]] static void query_thread()
	{
		using nlohmann::json;
		http_client client;

		client.set_headers({
			{ "Content-Type", "application/json" },
			{ "Access-Control-Allow-Origin", "*" },
		});

		while(true)
		{
			for(size_t i = 0; (i < 2000) and fast_reload.test_and_set(); i++)
			{
				scroll_progress.store(i);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			auto data = client.transfer(
				client.get,
				"http://glados.shack/siid/apps/powermeter.py?n=" + std::to_string(zoom_scale[zoom_level].value)
			);
			if(not data)
			{
				module::get<powerview>()->total_power = -1.0;
				continue;
			}
			try
			{
				auto cfg = json::parse(data->begin(), data->end());

				auto l1 = cfg["L1.Power"];
				auto l2 = cfg["L2.Power"];
				auto l3 = cfg["L3.Power"];
				auto time = cfg["Minutes ago"];

				if(l1.size() != l2.size())
					continue;
				if(l1.size() != l3.size())
					continue;
				if(l1.size() != time.size())
					continue;

				std::vector<powernode> new_nodes;
				for(size_t i = 0; i < l1.size(); i++)
				{
					auto & node = new_nodes.emplace_back();
					node.time = time[i].get<double>();
					node.phase[0] = l1[i].get<double>();
					node.phase[1] = l2[i].get<double>();
					node.phase[2] = l3[i].get<double>();
				}

				if(new_nodes.size() > 0)
				  module::get<powerview>()->total_power = new_nodes.back().total();
				else
					module::get<powerview>()->total_power = -1.0;

				*nodes.obtain() = std::move(new_nodes);
			}
			catch(...)
			{
				module::get<powerview>()->total_power = -1.0;
			}
		}
	}
}

void powerview::init()
{
	add_back_button();

	{
		auto * btn = add<button>();
		btn->bounds = { 30, 824, 170, 170 };
		btn->icon = IMG_LoadTexture(renderer, (resource_root / "icons" / "magnify-plus-outline.png" ).c_str());
		btn->color = { 0x03, 0xA9, 0xF4, 255 };
		btn->on_click = []() {
			/* zoom in */
			if(zoom_level > 0)
				zoom_level--;
			fast_reload.clear();
		};
	}
	{
		auto * btn = add<button>();
		btn->bounds = { 30, 624, 170, 170 };
		btn->icon = IMG_LoadTexture(renderer, (resource_root / "icons" / "magnify-minus-outline.png" ).c_str());
		btn->color = { 0x03, 0xA9, 0xF4, 255 };
		btn->on_click = [=]() {
			/* zoom out */
			if(zoom_level < (zoom_scale_cnt - 1))
				zoom_level++;
			fast_reload.clear();
		};
	}

	std::thread(query_thread).detach();
}

void powerview::render()
{
	SDL_Rect rect;
	gui_module::render();

	auto const nodes = ::nodes.obtain();

	SDL_Rect const window = { 220, 20, 1040, 984 };

	SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
	SDL_RenderFillRect(renderer, &window);

	double max = 0;
	for(size_t i = 0; i < nodes->size(); i++)
	{
		max = std::max(max, (*nodes)[i].total());
	}

	auto const max_power = 1000.0 * std::ceil(max / 1000.0);

	auto const get_point = [&](size_t idx, double f) -> SDL_Point
	{
		int const range = (nodes->size() - 2);
		float pos = int(idx) - 1;

		return SDL_Point {
			window.x + int(window.w * pos / range),
			window.y + int(window.h * (1.0 - f / max_power)),
		};
	};

	SDL_RenderSetClipRect(renderer, &window);

	for(size_t i = 1; i < nodes->size(); i++)
	{
		auto const & from = nodes->at(i - 1);
		auto const & to   = nodes->at(i - 0);

		for(size_t j = 0; j < 3; j++)
		{
			SDL_SetRenderDrawColor(renderer, (j==0)?255:0, (j==1)?255:0, (j==2)?255:0, 255);

			auto const p1 = get_point(i - 1, from.phase[j]);
			auto const p2 = get_point(i, to.phase[j]);
			SDL_RenderDrawLine(renderer,
				p1.x, p1.y,
				p2.x, p2.y
			);
		}
		{
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

			auto const p1 = get_point(i - 1, from.total());
			auto const p2 = get_point(i, to.total());
			SDL_RenderDrawLine(renderer,
				p1.x, p1.y,
				p2.x, p2.y
			);
		}
	}

	SDL_RenderSetClipRect(renderer, nullptr);

	{
		int h = TTF_FontHeight(rendering::small_font->font.get());

		for(int i = 1; i < (max_power / 1000); i++)
		{
			rect = { 240, int(get_point(0, 1000.0 * i).y - h/2), 65, h };

			SDL_SetRenderDrawColor(renderer, 32, 32, 32, 0x80);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_RenderFillRect(renderer, &rect);

			rendering::small_font->render(
				rect,
				std::to_string(1000 * i),
				FontRenderer::Middle | FontRenderer::Left
			);
		}
	}

	rect = { 240, 40, 200, 50 };
	rendering::medium_font->render(
		rect,
		zoom_scale[zoom_level].display,
		FontRenderer::Top | FontRenderer::Left
	);

	if(nodes->size() > 0)
	{
		rect = { 20, 220, 180, 64 };

		rendering::small_font->render(
			rect,
			"Total:",
			FontRenderer::Left | FontRenderer::Top,
			{ 0x80, 0x80, 0x80, 0xFF }
		);
		rect.y += TTF_FontHeight(rendering::small_font->font.get());

		rendering::big_font->render(
			rect,
			std::to_string(int(nodes->back().total())) + " W",
			FontRenderer::Center | FontRenderer::Top
		);
		rect.y += rect.h;

		rendering::small_font->render(
			rect,
			"L1:",
			FontRenderer::Left | FontRenderer::Top,
			{ 0x80, 0x80, 0x80, 0xFF }
		);
		rect.y += TTF_FontHeight(rendering::small_font->font.get());

		rendering::big_font->render(
			rect,
			std::to_string(int(nodes->back().phase[0])) + " W",
			FontRenderer::Center | FontRenderer::Top,
			{ 0xFF, 0x00, 0x00, 0xFF }
		);
		rect.y += rect.h;


		rendering::small_font->render(
			rect,
			"L2:",
			FontRenderer::Left | FontRenderer::Top,
			{ 0x80, 0x80, 0x80, 0xFF }
		);
		rect.y += TTF_FontHeight(rendering::small_font->font.get());

		rendering::big_font->render(
			rect,
			std::to_string(int(nodes->back().phase[1])) + " W",
			FontRenderer::Center | FontRenderer::Top,
			{ 0x00, 0xFF, 0x00, 0xFF }
		);
		rect.y += rect.h;

		rendering::small_font->render(
			rect,
			"L3:",
			FontRenderer::Left | FontRenderer::Top,
			{ 0x80, 0x80, 0x80, 0xFF }
		);
		rect.y += TTF_FontHeight(rendering::small_font->font.get());

		rendering::big_font->render(
			rect,
			std::to_string(int(nodes->back().phase[2])) + " W",
			FontRenderer::Center | FontRenderer::Top,
			{ 0x00, 0x00, 0xFF, 0xFF }
		);
	}
}
