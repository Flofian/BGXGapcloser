#include "../plugin_sdk/plugin_sdk.hpp"
#pragma once
namespace agc
{
	enum class gapcloser_type
	{
		skillshot,
		targeted,
		item
	};

	struct antigapcloser_args
	{
		gapcloser_type type;
		game_object_script target;

		std::string name;
		std::string spell_name;

		float start_time;
		float end_time;
		float speed;

		vector start_position;
		vector end_position;

		bool is_unstoppable;
		bool is_cc;


		antigapcloser_args() : type(gapcloser_type::skillshot), target(nullptr),
			name(""), spell_name(""), start_time(0.f), end_time(0.f), speed(0.f),
			is_unstoppable(false), is_cc(false)
		{

		}
	};

	typedef void(*gapcloser_handler)(game_object_script sender, antigapcloser_args* args);

	void add_event_handler(gapcloser_handler p_handler);
	void remove_event_handler(gapcloser_handler p_handler);
	void create_menu(TreeTab* tab);
}