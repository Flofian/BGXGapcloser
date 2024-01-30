#include "../plugin_sdk/plugin_sdk.hpp"
#include "gapclose.h"
namespace smolderfarm {
	TreeTab* mainmenu;
	TreeTab* tab1 = nullptr;
	
	bool checkMenu(std::string name) {
		auto m = tab1->get_entry(name);
		if (m) return m->get_bool();
		return false;
	}
	void on_gapcloser(game_object_script sender, agc::antigapcloser_args* args)
	{
		console->print("Sender: %s, Name: %s, SpellName: %s, Enabled: %s", sender->get_model_cstr(), args->name.c_str(), args->spell_name.c_str(), checkMenu(args->spell_name) ? "True" : "False");
	}

	void load() {
		mainmenu = menu->create_tab("GapcloserExample", "GapcloserExample");
		tab1 = mainmenu->add_tab("agc", "Gapcloser Whitelist");
		agc::create_menu(tab1);
		agc::add_event_handler(on_gapcloser);
	}

	void unload() {

		agc::remove_event_handler(on_gapcloser);
	}
}