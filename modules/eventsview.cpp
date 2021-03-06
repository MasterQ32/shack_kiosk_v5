#include "eventsview.hpp"
#include "http_client.hpp"
#include "rendering.hpp"
#include "protected_value.hpp"
#include "rect_tools.hpp"

#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>
#include <atomic>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <iostream>

namespace
{

	protected_value<std::vector<eventsview::Event>> events;

	void fetch(http_client & client)
	{
		auto raw = client.transfer(
			client.get,
			"https://events-api.shackspace.de/events/"
		);
		if(not raw)
		{
			*events.obtain() = {
			    eventsview::Event
					{
						.title = "Keine Verbindung zu events.shackspace.de",
						.start = std::time(nullptr),
						.end = std::time(nullptr),
					}
			};
			return;
		}
		try
		{
			auto const json = nlohmann::json::parse(raw->begin(), raw->end());
			std::vector<eventsview::Event> list;

			for(auto const & val : json)
			{
				auto & event = list.emplace_back();

				std::stringstream date_start { std::string(val["start"]) };
				std::stringstream date_end   { std::string(val["end"]) };

				std::tm tm_start {};
				std::tm tm_end {};
				// 2018-04-09T15:06:25.338943+02:00

				date_start >> std::get_time(&tm_start, "%Y-%m-%dT%H:%M:%S");
				date_end >> std::get_time(&tm_end, "%Y-%m-%dT%H:%M:%S");

				event.title = val["name"];
				event.start = std::mktime(&tm_start);
				event.end = std::mktime(&tm_end);
			}

			std::time_t now_t;
			{
				auto pos_t = std::time(nullptr);
				auto pos = *std::gmtime(&pos_t);
				pos.tm_min = 0;
				pos.tm_hour = 0;
				pos.tm_sec = 0;
				pos.tm_isdst = 0;
				now_t = std::mktime(&pos);
				assert(now_t != -1);
			}

			// erase all events in the past
			list.erase(std::remove_if(list.begin(), list.end(), [&](eventsview::Event const & e) {
				// auto const day = *std::localtime(&e.start);
				// return (day.tm_yday < now.tm_yday) and (day.tm_year <= now.tm_yday);
				return std::difftime(e.end, now_t) < 0;
			}), list.end());

			// erase all events in the "far" future (1 week)
//			list.erase(std::remove_if(list.begin(), list.end(), [&](Event const & e) {
//				return std::difftime(e.start, now_t) >= (3600 * 24 * 7);
//			}), list.end());

			// sort list by start of event
			std::sort(list.begin(), list.end(), [](eventsview::Event const & e1, eventsview::Event const & e2)
			{
				return std::difftime(e1.start, e2.start) < 0;
			});

			// limit the list to 20 entries
			if(list.size() > 10)
				list.resize(10);

			*events.obtain() = std::move(list);
		}
		catch(...)
		{

		}
	}

	[[noreturn]] void task()
	{
		http_client client;
		client.set_headers({
			{ "Content-Type", "application/json" },
			{ "Access-Control-Allow-Origin", "*" },
		});

		while(true)
		{
			try
			{
				fetch(client);
			}
			catch (...)
			{
			}
			std::this_thread::sleep_for(std::chrono::minutes(10));
		}
	}
}

std::optional<eventsview::Event> eventsview::current_event() const
{
	auto handle = events.obtain();
	if(handle->size() == 0)
		return std::nullopt;
	auto const ev = handle->front();
	auto const now = std::time(nullptr);
	if((std::difftime(now, ev.start) > 0) and (std::difftime(ev.end, now) < 0))
		return std::move(ev);
	else
		return std::nullopt;
}

void eventsview::init()
{
	add_back_button();
	std::thread(task).detach();
}

void eventsview::render()
{
	gui_module::render();

	auto const & font = *rendering::small_font;

	SDL_Rect rect = { 220, 10, 1050, 70 };
	bool odd = false;
	auto const now = std::time(nullptr);
	for(auto const & ev : *events.obtain())
	{
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

		if((std::difftime(now, ev.start) > 0) and (std::difftime(ev.end, now) < 0))
			SDL_SetRenderDrawColor(renderer, 0x00, 0xFF, 0x00, odd ? 0x10 : 0x20);
		else
			SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, odd ? 0x10 : 0x20);
		SDL_RenderFillRect(renderer, &rect);

		auto const tm = *std::localtime(&ev.start);
		auto const duration = std::difftime(ev.end, ev.start);
		char buffer[256];
		snprintf(buffer, sizeof buffer, "%02d.%02d.%04d %02d:%02d", tm.tm_mday, 1+tm.tm_mon, 1900+tm.tm_year, tm.tm_hour, tm.tm_min);

		std::string duration_text;
		if(duration < 3600)
			duration_text = std::to_string(int(std::ceil(duration / 60))) + " Min.";
		else if(duration < 24 * 3600)
			duration_text = std::to_string(int(std::ceil(duration / 3600))) + " Std.";
		else
			duration_text = std::to_string(int(std::ceil(duration / (3600 * 24)))) + " Tage";

		auto padded_rect = add_margin(rect, 10);

		auto const [ text_prefix, text_postfix ] = split_horizontal(padded_rect, 100);

		font.render(
			text_prefix,
			duration_text,
			font.Left | font.Middle,
			{ 0xFF, 0xFF, 0xFF, 0x80 }
		);

		font.render(
			text_postfix,
			ev.title,
			font.Left | font.Middle
		);

		font.render(
			text_postfix,
			buffer,
			font.Right | font.Middle
		);

		rect.y += rect.h;
		odd = !odd;
	}
}
