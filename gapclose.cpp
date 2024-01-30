#include "../plugin_sdk/plugin_sdk.hpp"
#include "gapclose.h"
namespace agc
{
#define DEBUG_FLAG false
#define ADD_DASH_DATA_VAR(TYPE, NAME) TYPE NAME = {}; TrackedDashData& set_##NAME( const TYPE& NAME ) { this->NAME = NAME; return *this; }

	struct TrackedDashData
	{
		ADD_DASH_DATA_VAR(std::string, name)
			ADD_DASH_DATA_VAR(std::string, spell_name)

			ADD_DASH_DATA_VAR(std::uint32_t, required_buffhash)
			ADD_DASH_DATA_VAR(std::uint32_t, spell_name_hash)

			ADD_DASH_DATA_VAR(bool, wait_for_new_path)
			ADD_DASH_DATA_VAR(bool, is_dangerous)
			ADD_DASH_DATA_VAR(bool, is_fixed_range)
			ADD_DASH_DATA_VAR(bool, is_targeted)
			ADD_DASH_DATA_VAR(bool, is_inverted)
			ADD_DASH_DATA_VAR(bool, find_target_by_buffhash)
			ADD_DASH_DATA_VAR(bool, wait_for_targetable)
			ADD_DASH_DATA_VAR(bool, is_cc)
			ADD_DASH_DATA_VAR(bool, is_unstoppable)

			ADD_DASH_DATA_VAR(float, delay)
			ADD_DASH_DATA_VAR(float, speed)
			ADD_DASH_DATA_VAR(float, range)
			ADD_DASH_DATA_VAR(float, min_range)
			ADD_DASH_DATA_VAR(float, extra_range)
			ADD_DASH_DATA_VAR(float, add_ms_ratio)
			ADD_DASH_DATA_VAR(float, always_fixed_delay)

			TrackedDashData()
		{
			this->speed = FLT_MAX;
			this->spell_name_hash = spell_hash_real(spell_name.c_str());
		}
	};

	struct TrackedDash
	{
		game_object_script sender;
		game_object_script target;

		const TrackedDashData* dash_data;

		float start_time;
		float end_time;
		float speed;

		vector start_position;
		vector end_position;

		bool is_finished_detecting;

		TrackedDash()
		{
			this->sender = nullptr;
			this->target = nullptr;

			this->dash_data = nullptr;

			this->start_time = 0;
			this->end_time = 0;
			this->speed = 0;

			this->is_finished_detecting = false;
		}
	};

	std::vector< TrackedDash > detected_dashes;
	std::vector< TrackedDashData > dashes_data;

	std::vector<void*> p_handlers;

	TrackedDashData& add_dash(const std::string& spell_name, float range, float speed)
	{
		TrackedDashData data;
		data.spell_name = spell_name;
		data.range = range;
		data.speed = speed;

		dashes_data.push_back(data);

		return dashes_data[dashes_data.size() - 1];
	}

	void OnProcessSpellCast(game_object_script sender, spell_instance_script spell)
	{
		if (sender->is_enemy() || DEBUG_FLAG)
		{
			auto name = spell->get_spell_data()->get_name();
			auto it = std::find_if(dashes_data.begin(), dashes_data.end(), [&name](const TrackedDashData& x) { return x.spell_name == name; });

			if (it != dashes_data.end())
			{
				game_object_script target = spell->get_last_target_id() != 0 && it->is_targeted
					? entitylist->get_object(spell->get_last_target_id())
					: nullptr;

				if (it->find_target_by_buffhash)
				{
					for (auto&& t : sender->is_ally() ? entitylist->get_enemy_heroes() : entitylist->get_ally_heroes())
					{
						if (t->is_valid_target() && t->has_buff(it->required_buffhash))
						{
							target = t;
							break;
						}
					}
				}

				if (it->is_targeted && target == nullptr)
				{
					return;
				}

				if (target && it->required_buffhash && !target->has_buff(it->required_buffhash))
				{
					return;
				}

				auto start = spell->get_start_position();
				auto end = spell->get_end_position();

				if (it->min_range > 0 && start.distance_squared(end) < std::powf(it->min_range, 2))
				{
					end = start.extend(end, it->min_range);
				}

				if (it->is_fixed_range || start.distance_squared(end) > std::powf(it->range, 2))
				{
					end = start.extend(end, it->range);
				}

				if (it->is_inverted)
				{
					end = start - (end - start);
				}

				if (target && !it->is_fixed_range)
				{
					end = target->get_position();
				}

				if (it->extra_range > 0)
				{
					end = end.extend(start, -it->extra_range);
				}

				TrackedDash new_dash;
				new_dash.sender = sender;
				new_dash.target = target;
				new_dash.dash_data = &(*it);
				new_dash.start_position = start;
				new_dash.end_position = end;
				new_dash.speed = it->speed + sender->get_move_speed() * it->add_ms_ratio;
				float delay = it->delay;
				if (sender->get_champion() == champion_id::Belveth)
				{
					auto belveth_speed = 750 + 50 * sender->get_spell(spellslot::q)->level() + sender->get_move_speed();

					if (sender->has_buff(buff_hash("BelvethRSteroid")))
						belveth_speed += belveth_speed / 10.f;

					new_dash.speed = belveth_speed;
				}
				if (sender->get_champion() == champion_id::Yone) {
					// this is really garbage performance wise, but i only call it once per yone e2 cast, so it should be fine
					if (sender->get_mana() > 0 && spell->get_spell_data()->get_name_hash() == spell_hash("YoneE")) {
						for (int i = 0; i <= entitylist->get_max_objects(); ++i)
						{
							const auto entity = entitylist->get_object(i);
							if (entity && entity->is_valid() && entity->get_emitter_resources_hash() == buff_hash("Yone_E_Beam") && entity->get_emitter()->get_handle() == sender->get_handle())
							{
								new_dash.end_position = entity->get_position();
								end = entity->get_position();
								new_dash.speed = 3000.f;
								delay = 0.25;
							}
						}
					}
					if (spell->get_spell_data()->get_name_hash() == spell_hash("YoneQ3")) {
						delay = fmax(0.175f, 0.35 - (0.035 * (sender->mAttackSpeedMod() - 1) / 0.24));
					}
				}

				if (it->always_fixed_delay > 0)
					new_dash.speed = new_dash.start_position.distance(new_dash.end_position) / it->always_fixed_delay;

				new_dash.start_time = gametime->get_time();
				new_dash.end_time = new_dash.start_time + delay + start.distance(end) / new_dash.speed;

				if (it->wait_for_targetable)
					new_dash.end_time = new_dash.start_time + 2.5f;

				new_dash.is_finished_detecting = !it->wait_for_new_path && !it->wait_for_targetable;
				detected_dashes.push_back(new_dash);
			}
		}
	}

	void OnNewPath(game_object_script sender, const std::vector<vector>& path, bool is_dash, float dash_speed)
	{
		if (is_dash)
		{
			float length = path.size() > 1 ? geometry::geometry::path_length(path) : 0;

			for (TrackedDash& dash : detected_dashes)
			{
				if (dash.is_finished_detecting || !dash.dash_data->wait_for_new_path || sender != dash.sender) continue;

				dash.start_time = gametime->get_time() - dash.dash_data->delay;
				dash.end_time = gametime->get_time() + length / dash_speed;
				dash.start_position = path.front();
				dash.end_position = path.back();
				dash.speed = dash_speed;
				dash.is_finished_detecting = true;
			}
		}
	}

	void OnUpdate()
	{
		detected_dashes.erase(std::remove_if(detected_dashes.begin(), detected_dashes.end(), [](const TrackedDash& dash)
			{
				return gametime->get_time() >= dash.end_time;
			}), detected_dashes.end());

		for (TrackedDash& dash : detected_dashes)
		{
			if (dash.is_finished_detecting || !dash.dash_data->wait_for_targetable) continue;
			if (gametime->get_time() - dash.start_time < 0.15f) continue;

			if (dash.sender->is_targetable())
			{
				if (dash.sender->get_distance(myhero) > 150)
				{
					dash.end_time = gametime->get_time() - 0.1f; //delete Elise E dash, it was not casted on me
					continue;
				}

				dash.end_position = dash.sender->get_position();

				if (dash.dash_data->always_fixed_delay > 0)
					dash.speed = dash.start_position.distance(dash.end_position) / dash.dash_data->always_fixed_delay;

				dash.start_time = gametime->get_time();
				dash.end_time = dash.start_time + dash.dash_data->always_fixed_delay;
				dash.is_finished_detecting = true;
			}
		}

		for (const TrackedDash& dash : detected_dashes)
		{
			if (!dash.is_finished_detecting || !dash.sender->is_valid()) continue;
			// I use is_valid instead of is_valid_target, no reason to ignore invulnerable targets
			//if ( ( dash.target == nullptr || !dash.target->is_me( ) ) && dash.sender->get_distance( myhero ) > 500 ) continue;
			// I ignore this line, should be up to the handler
			antigapcloser_args args;
			args.type = gapcloser_type::skillshot;
			args.target = dash.target;
			args.start_time = dash.start_time;
			args.end_time = dash.end_time;
			args.speed = dash.speed;
			args.start_position = dash.start_position;
			args.end_position = dash.end_position;
			args.is_unstoppable = dash.dash_data->is_unstoppable;
			args.is_cc = dash.dash_data->is_cc;
			args.name = dash.dash_data->name;
			args.spell_name = dash.dash_data->spell_name;

			if (!dash.dash_data->name.empty())
				args.type = gapcloser_type::item;

			if (dash.target != nullptr && dash.target->is_me())
				args.type = gapcloser_type::targeted;

			for (auto const& callback : p_handlers)
			{
				if (callback != nullptr)
				{
					reinterpret_cast<gapcloser_handler>(callback)(dash.sender, &args);
				}
			}
		}
	}

	void OnCreate(game_object_script obj) {
		if (obj && obj->is_valid())
		{
			auto spellname = "";
			game_object_script t = nullptr;
			if (obj->get_emitter_resources_hash() == buff_hash("Sylas_E_chain_move")) {
				spellname = "SylasE2";
				t = obj->get_particle_attachment_object();
			}
			else if (obj->get_emitter_resources_hash() == buff_hash("Leona_E_mis_dash")) {
				spellname = "LeonaZenithBlade";
				t = obj->get_particle_target_attachment_object();
			}
			auto it = std::find_if(dashes_data.begin(), dashes_data.end(), [spellname](const TrackedDashData& x) { return x.spell_name == spellname; });
			if (it == dashes_data.end()) return;
			TrackedDash new_dash;
			new_dash.sender = obj->get_emitter();
			new_dash.target = t;
			new_dash.dash_data = &(*it);
			new_dash.start_position = new_dash.sender->get_position();
			new_dash.end_position = new_dash.target->get_position();
			new_dash.speed = it->speed; 
			new_dash.start_time = gametime->get_time();
			new_dash.end_time = new_dash.start_time + new_dash.start_position.distance(new_dash.end_position) / new_dash.speed;

			new_dash.is_finished_detecting = !it->wait_for_new_path && !it->wait_for_targetable;
			detected_dashes.push_back(new_dash);
		}
		
	}


	void on_draw() {
		if (!DEBUG_FLAG) return;
		for (TrackedDash& dash : detected_dashes) {
			draw_manager->add_line(dash.start_position, dash.end_position, MAKE_COLOR(255, 0, 0, 255), 3);
			draw_manager->add_circle(dash.start_position, 20, MAKE_COLOR(255, 255, 0, 255), 3);
			draw_manager->add_circle(dash.end_position, 20, MAKE_COLOR(0, 255, 0, 255), 3);
		}
	}


	void add_event_handler(gapcloser_handler p_handler)
	{
		auto it = std::find(p_handlers.begin(), p_handlers.end(), (void*)p_handler);

		if (it == p_handlers.end())
		{
			p_handlers.push_back((void*)p_handler);
		}

		if (p_handlers.size() == 1)
		{
			add_dash("3152Active", 300.f, 1150.f).set_name("Hextech Rocketbelt").set_is_fixed_range(true);
			//add_dash("6671Cast", 425.f, 1350.f).set_name("Prowler's Claw").set_min_range(200.f);
			//add_dash("6693Active", 500.f, 2000.f).set_name("Galeforce").set_is_targeted(true).set_delay(0.2f);
			// those items dont exist anymore

			auto list = DEBUG_FLAG ? entitylist->get_all_heroes() : entitylist->get_enemy_heroes();
			for (auto& hero : list)
			{
				switch (hero->get_champion())
				{
				case champion_id::Aatrox:
					add_dash("AatroxE", 300.f, 800.f).set_wait_for_new_path(true);
					break;
				case champion_id::Ahri:
					add_dash("AhriTumble", 500.f, 1200.f).set_is_fixed_range(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Akali:
					add_dash("AkaliE", 350.f, 1400.f).set_is_fixed_range(true).set_is_inverted(true).set_delay(0.2f);
					add_dash("AkaliEb", FLT_MAX, 1700.f).set_is_targeted(true).set_required_buffhash(buff_hash("AkaliEMis")).set_find_target_by_buffhash(true);
					add_dash("AkaliR", 750.f, 1500.f).set_is_fixed_range(true);
					add_dash("AkaliRb", 800.f, 3000.f).set_is_fixed_range(true);
					break;
				case champion_id::Alistar:
					add_dash("Headbutt", 650.f, 1500.f).set_is_targeted(true).set_is_cc(true);
					break;
				case champion_id::Caitlyn:
					add_dash("CaitlynE", 390.f, 1000.f).set_is_fixed_range(true).set_is_inverted(true).set_delay(0.15f);
					break;
				case champion_id::Camille:
					add_dash("CamilleEDash2", 800.f, 1050.f).set_is_cc(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Corki:
					add_dash("CarpetBomb", 600.f, 650.f).set_min_range(300.f).set_add_ms_ratio(1.f);
					add_dash("CarpetBombMega", 2000.f, 1500.f).set_min_range(1200.f);
					break;
				case champion_id::Diana:
					add_dash("DianaTeleport", 825.f, 2500.f).set_is_targeted(true);
					break;
				case champion_id::Ekko:
					add_dash("EkkoE", 350.f, 1150.f).set_is_fixed_range(true);
					add_dash("EkkoEAttack", 600.f, 2000.f).set_is_targeted(true).set_delay(0.1f);
					break;
				case champion_id::Elise:
					add_dash("EliseSpiderQCast", 475.f, 1200.f).set_is_targeted(true);
					add_dash("EliseSpiderE", 700.f, 1000.f).set_is_targeted(true).set_wait_for_targetable(true).set_always_fixed_delay(0.5f);
					break;
				case champion_id::Evelynn:
					add_dash("EvelynnE2", 400.f, 1900.f).set_is_targeted(true);
					break;
				case champion_id::Fiora:
					add_dash("FioraQ", 450.f, 500.f).set_add_ms_ratio(2.f);
					break;
				case champion_id::Fizz:
					add_dash("FizzQ", 550.f, 1400.f).set_is_fixed_range(true).set_is_targeted(true);
					break;
				case champion_id::Galio:
					add_dash("GalioE", 650.f, 2300.f).set_is_cc(true).set_delay(0.4f);
					break;
				case champion_id::Gnar:
					add_dash("GnarE", 475.f, 900.f);
					add_dash("GnarBigE", 675.f, 1165.f);
					break;
				case champion_id::Gragas:
					add_dash("GragasE", 600.f, 900.f).set_is_cc(true).set_is_fixed_range(true);
					break;
				case champion_id::Graves:
					add_dash("GravesMove", 375.f, 1150.f).set_wait_for_new_path(true);
					break;
				case champion_id::Gwen:
					add_dash("GwenE", 350.f, 1050.f).set_add_ms_ratio(1.f);
					break;
				case champion_id::Hecarim:
					add_dash("HecarimRampAttack", 900.f, 1200.f).set_is_cc(true).set_is_targeted(true);
					add_dash("HecarimUlt", 1000.f, 1100.f).set_is_cc(true).set_is_unstoppable(true).set_min_range(300.f);
					break;
				case champion_id::Illaoi:
					add_dash("IllaoiWAttack", 300.f, 800.f).set_is_targeted(true);
					break;
				case champion_id::Irelia:
					add_dash("IreliaQ", 600.f, 1400.f).set_is_targeted(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::JarvanIV:
					add_dash("JarvanIVDragonStrike", 850.f, 2000.f).set_is_cc(true).set_wait_for_new_path(true).set_delay(0.4f);
					break;
				case champion_id::Jax:
					add_dash("JaxQ", 700.f, 1600.f).set_is_targeted(true);
					break;
				case champion_id::Jayce:
					add_dash("JayceToTheSkies", 600.f, 1000.f).set_is_targeted(true);
					break;
				case champion_id::Kaisa:
					add_dash("KaisaR", 3000.f, 3700.f);
					break;
				case champion_id::Kayn:
					add_dash("KaynQ", 350.f, 1150.f);
					add_dash("KaynRJumpOut", 500.f, 1200.f).set_wait_for_new_path(true).set_delay(3.f);
					break;
				case champion_id::Khazix:
					add_dash("KhazixE", 700.f, 1250.f);
					add_dash("KhazixELong", 850.f, 1250.f);
					break;
				case champion_id::Kindred:
					add_dash("KindredQ", 300.f, 500.f).set_add_ms_ratio(1.f);
					break;
				case champion_id::Kled:
					add_dash("KledRiderQ", 300.f, 1000.f).set_is_inverted(true).set_delay(0.25f);
					add_dash("KledEDash", 700.f, 600.f).set_add_ms_ratio(1.f);
					break;
				case champion_id::Leblanc:
					add_dash("LeblancW", 600.f, 1450.f);
					add_dash("LeblancRW", 600.f, 1450.f);
					break;
				case champion_id::LeeSin:
					add_dash("BlindMonkQTwo", 2000.f, 2000.f).set_is_targeted(true).set_required_buffhash(buff_hash("BlindMonkQOne")).set_find_target_by_buffhash(true);
					add_dash("BlindMonkWOne", 700.f, 1350.f).set_is_targeted(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Leona:
					add_dash("LeonaZenithBlade", 900.f, 2000.f).set_is_targeted(true);
					break;
				case champion_id::Lillia:
					add_dash("LilliaW", 500.f, 1000.f).set_always_fixed_delay(0.8f);
					break;
				case champion_id::Lucian:
					add_dash("LucianE", 475.f, 1350.f).set_wait_for_new_path(true);
					break;
				case champion_id::Malphite:
					add_dash("UFSlash", 1000.f, 1500.f).set_is_cc(true).set_is_unstoppable(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Maokai:
					add_dash("MaokaiW", 525.f, 1300.f).set_is_cc(true).set_is_unstoppable(true).set_is_targeted(true);
					break;
				case champion_id::MonkeyKing:
					add_dash("MonkeyKingDecoy", 300.f, 1240.f).set_is_fixed_range(true);
					add_dash("MonkeyKingNimbus", 625.f, 1050.f).set_is_targeted(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Nidalee:
					add_dash("Pounce", 750.f, 950.f).set_wait_for_new_path(true).set_min_range(350.f);
					break;
				case champion_id::Ornn:
					add_dash("OrnnE", 650.f, 1600.f).set_is_cc(true).set_is_fixed_range(true).set_delay(0.35f);
					break;
				case champion_id::Pantheon:
					add_dash("PantheonW", 600.f, 1100.f).set_is_cc(true).set_is_targeted(true);
					break;
				case champion_id::Poppy:
					add_dash("PoppyE", 475.f, 1800.f).set_is_cc(true).set_is_targeted(true);
					break;
				case champion_id::Pyke:
					add_dash("PykeE", 550.f, 2000.f).set_is_cc(true).set_is_fixed_range(true);
					break;
				case champion_id::Qiyana:
					add_dash("QiyanaE", 550.f, 1100.f).set_is_fixed_range(true).set_is_targeted(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Rakan:
					add_dash("RakanW", 650.f, 1700.f).set_is_cc(true).set_always_fixed_delay(0.85f);
					break;
				case champion_id::Rammus:
					add_dash("Tremors2", 1500.f, 1000.f).set_is_unstoppable(true).set_always_fixed_delay(0.85f);
					break;
				case champion_id::RekSai:
					add_dash("RekSaiEBurrowed", 800.f, 800.f).set_is_fixed_range(true);
					break;
				case champion_id::Rell:
					add_dash("RellW_Dismount", 500.f, 1000.f).set_is_cc(true).set_always_fixed_delay(0.85f);
					break;
				case champion_id::Renekton:
					add_dash("RenektonDice", 450.f, 760.f).set_is_fixed_range(true).set_add_ms_ratio(1.f);
					add_dash("RenektonSliceAndDice", 450.f, 760.f).set_is_fixed_range(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Riven:
					add_dash("RivenFeint", 250.f, 1200.f).set_is_fixed_range(true);
					break;
				case champion_id::Samira:
					add_dash("SamiraE", 650.f, 1600.f).set_is_fixed_range(true).set_is_targeted(true);
					break;
				case champion_id::Sejuani:
					add_dash("SejuaniQ", 650.f, 1000.f).set_is_cc(true);
					break;
				case champion_id::Shen:
					add_dash("ShenE", 600.f, 800.f).set_is_cc(true).set_min_range(300.f).set_add_ms_ratio(1.f);
					break;
				case champion_id::Shyvana:
					add_dash("ShyvanaTransformLeap", 950.f, 1100.f).set_is_unstoppable(true);
					break;
				case champion_id::Sylas:
					add_dash("SylasW", 400.f, 1450.f).set_is_targeted(true);
					add_dash("SylasE", 400.f, 1450.f);
					add_dash("SylasE2", 800.f, 1950.f).set_is_cc(true).set_is_targeted(true);
					break;
				case champion_id::Talon:
					add_dash("TalonQ", 575.f, 1600.f).set_is_targeted(true);
					break;
				case champion_id::Tristana:
					add_dash("TristanaW", 900.f, 1100.f).set_delay(0.25f);
					break;
				case champion_id::Tryndamere:
					add_dash("TryndamereE", 660.f, 900.f);
					break;
				case champion_id::Urgot:
					add_dash("UrgotE", 450.f, 1200.f).set_is_cc(true).set_is_fixed_range(true).set_delay(0.45f).set_add_ms_ratio(1.f);
					break;
				case champion_id::Vayne:
					add_dash("VayneTumble", 300.f, 500.f).set_is_fixed_range(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Vi:
					add_dash("ViQ", 725.f, 1400.f).set_is_cc(true).set_wait_for_new_path(true);
					break;
				case champion_id::Viego:
					add_dash("ViegoR", 500.f, 1000.f).set_is_unstoppable(true).set_always_fixed_delay(0.6f);
					add_dash("ViegoW", 300.f, 1000.f).set_is_cc(true).set_is_fixed_range(true);
					break;
				case champion_id::Volibear:
					add_dash("VolibearR", 700.f, 1000.f).set_is_unstoppable(true).set_always_fixed_delay(1.f);
					break;
				case champion_id::XinZhao:
					add_dash("XinZhaoEDash", 1100.f, 2500.f).set_is_targeted(true);
					break;
				case champion_id::Yasuo:
					add_dash("YasuoEDash", 475.f, 750.f).set_is_fixed_range(true).set_is_targeted(true).set_add_ms_ratio(0.875f);
					break;
				case champion_id::Yone:
					add_dash("YoneQ3", 450, 1500).set_is_fixed_range(true);
					add_dash("YoneE", 300.f, 1200.f).set_is_fixed_range(true);
					break;
				case champion_id::Zac:
					add_dash("ZacE", 1800.f, 1000.f).set_wait_for_new_path(true);
					break;
				case champion_id::Zed:
					add_dash("ZedR", 625.f, 1000.f).set_is_targeted(true).set_always_fixed_delay(1.6f);
					break;
				case champion_id::Zeri:
					add_dash("ZeriE", 2000.f, 600.f).set_wait_for_new_path(true).set_add_ms_ratio(1.f);
					break;
				case champion_id::Belveth:
					add_dash("BelvethQ", 400.f, 800.f + hero->get_move_speed()).set_wait_for_new_path(true);
					break;
				case champion_id::Nilah:
					add_dash("NilahE", 450.f, 2200.f).set_is_targeted(true);
					break;
				default: break;
				}

			}

			event_handler< events::on_new_path >::add_callback(OnNewPath);
			event_handler< events::on_process_spell_cast >::add_callback(OnProcessSpellCast);
			event_handler< events::on_update >::add_callback(OnUpdate);
			event_handler< events::on_create_object >::add_callback(OnCreate);
			if(DEBUG_FLAG) event_handler< events::on_draw >::add_callback(on_draw);
		}
	}

	void remove_event_handler(gapcloser_handler p_handler)
	{
		auto it = std::find(p_handlers.begin(), p_handlers.end(), p_handler);

		if (it != p_handlers.end())
		{
			p_handlers.erase(it);
		}

		if (p_handlers.empty())
		{
			event_handler< events::on_new_path >::remove_handler(OnNewPath);
			event_handler< events::on_process_spell_cast >::remove_handler(OnProcessSpellCast);
			event_handler< events::on_update >::remove_handler(OnUpdate);
			event_handler< events::on_create_object >::remove_handler(OnCreate);
			if (DEBUG_FLAG) event_handler< events::on_draw >::remove_handler(on_draw);
		}
	}
	auto spell_icon(const char* name, int id=0) -> uint32_t*
	{
		auto spell = database->get_spell_by_hash(spell_hash_real(name));
		return spell ? spell->get_icon_texture_by_index(id) : nullptr;
	}
	void create_menu(TreeTab* tab) {
		TreeEntry* x = nullptr;
		x = tab->add_checkbox("3152Active", "Hextech Rocketbelt", false);
		auto rocketbelt = database->get_item_by_id(ItemId::Hextech_Rocketbelt);
		auto rbtexture = rocketbelt ? rocketbelt->get_texture() : std::make_pair(nullptr, vector4());
		auto descriptor_rocketbelt = create_texture_descriptor(rbtexture.first, ImVec4(rbtexture.second.x, rbtexture.second.y, rbtexture.second.z, rbtexture.second.w));
		x->set_texture(descriptor_rocketbelt);
		auto list = DEBUG_FLAG ? entitylist->get_all_heroes() : entitylist->get_enemy_heroes();
		for (auto& hero : list)
		{
			switch (hero->get_champion())
			{
			case champion_id::Aatrox:
				x = tab->add_checkbox("AatroxE", "Aatrox E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Ahri:
				x = tab->add_checkbox("AhriTumble", "Ahri R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::Akali:
				x = tab->add_checkbox("AkaliE", "Akali E1", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture_by_index(0));
				x = tab->add_checkbox("AkaliEb", "Akali E2", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture_by_index(1));
				x = tab->add_checkbox("AkaliR", "Akali R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture_by_index(0));
				x = tab->add_checkbox("AkaliRb", "Akali R", true);
				x->set_texture(spell_icon("AkaliRb"));
				break;
			case champion_id::Alistar:
				x = tab->add_checkbox("Headbutt", "Alistar W", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				break;
			case champion_id::Caitlyn:
				x = tab->add_checkbox("CaitlynE", "Caitlyn E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Camille:
				x = tab->add_checkbox("CamilleEDash2", "Camille E2", true);
				x->set_texture(spell_icon("CamilleEDash2"));
				break;
			case champion_id::Corki:
				x = tab->add_checkbox("CarpetBomb", "Corki W", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture_by_index(0));
				x = tab->add_checkbox("CarpetBombMega", "Corki W Package", true);
				x->set_texture(spell_icon("CarpetBombMega"));
				break;
			case champion_id::Diana:
				x = tab->add_checkbox("DianaTeleport", "Diana E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Ekko:
				x = tab->add_checkbox("EkkoE", "Ekko E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				x = tab->add_checkbox("EkkoEAttack", "Ekko E Attack", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Elise:
				x = tab->add_checkbox("EliseSpiderQCast", "Elise Spider Q", true);
				x->set_texture(spell_icon("EliseSpiderQCast"));
				x = tab->add_checkbox("EliseSpiderE", "Elise Spider E", true);
				x->set_texture(spell_icon("EliseSpiderE"));
				break;
			case champion_id::Evelynn:
				x = tab->add_checkbox("EvelynnE2", "Evelynn E", true);
				x->set_texture(spell_icon("EvelynnE2"));
				break;
			case champion_id::Fiora:
				x = tab->add_checkbox("FioraQ", "Fiora Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Fizz:
				x = tab->add_checkbox("FizzQ", "Fizz Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Galio:
				x = tab->add_checkbox("GalioE", "Galio E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Gnar:
				x = tab->add_checkbox("GnarE", "Mini Gnar E", true);
				x->set_texture(spell_icon("GnarE"));
				x = tab->add_checkbox("GnarBigE", "Big Gnar E", true);
				x->set_texture(spell_icon("GnarBigE"));
				break;
			case champion_id::Gragas:
				x = tab->add_checkbox("GragasE", "Gragas E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Graves:
				x = tab->add_checkbox("GravesMove", "Graves E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Gwen:
				x = tab->add_checkbox("GwenE", "Gwen E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Hecarim:
				x = tab->add_checkbox("HecarimRampAttack", "Hecarim E Attack", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				x = tab->add_checkbox("HecarimUlt", "Hecarim R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::Illaoi:
				x = tab->add_checkbox("IllaoiWAttack", "Illaoi W Attack", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				break;
			case champion_id::Irelia:
				x = tab->add_checkbox("IreliaQ", "Irelia Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::JarvanIV:
				x = tab->add_checkbox("JarvanIVDragonStrike", "Jarvan EQ", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Jax:
				x = tab->add_checkbox("JaxQ", "Jax Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Jayce:
				x = tab->add_checkbox("JayceToTheSkies", "Jayce Hammer Q", true);
				x->set_texture(spell_icon("JayceToTheSkies"));
				break;
			case champion_id::Kaisa:
				x = tab->add_checkbox("KaisaR", "Kaisa R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::Kayn:
				x = tab->add_checkbox("KaynQ", "Kayn Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture_by_index(0));
				x = tab->add_checkbox("KaynRJumpOut", "Kayn R Exit", true);
				x->set_texture(spell_icon("KaynRJumpOut"));
				break;
			case champion_id::Khazix:
				x = tab->add_checkbox("KhazixE", "KhaZix E", true);
				x->set_texture(spell_icon("KhazixE"));
				x = tab->add_checkbox("KhazixELong", "KhaZix E Long", true);
				x->set_texture(spell_icon("KhazixELong"));
				break;
			case champion_id::Kindred:
				x = tab->add_checkbox("KindredQ", "Kindred Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Kled:
				x = tab->add_checkbox("KledRiderQ", "Kled Shotgun Q", true);
				x->set_texture(spell_icon("KledRiderQ"));
				x = tab->add_checkbox("KledEDash", "Kled E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture_by_index(0));	// here it works (index 1 is greyed out)
				break;
			case champion_id::Leblanc:
				x = tab->add_checkbox("LeblancW", "Leblanc W", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				x = tab->add_checkbox("LeblancRW", "Leblanc RW", true);
				x->set_texture(spell_icon("LeblancRW"));
				break;
			case champion_id::LeeSin:
				x = tab->add_checkbox("BlindMonkQTwo", "Lee Sin Q2", true);
				x->set_texture(spell_icon("BlindMonkQTwo"));
				x = tab->add_checkbox("BlindMonkWOne", "Lee Sin W", true);
				x->set_texture(spell_icon("BlindMonkWOne"));
				break;
			case champion_id::Leona:
				x = tab->add_checkbox("LeonaZenithBlade", "Leona E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Lillia:
				x = tab->add_checkbox("LilliaW", "Lillia W", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				break;
			case champion_id::Lucian:
				x = tab->add_checkbox("LucianE", "Lucian E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Malphite:
				x = tab->add_checkbox("UFSlash", "Malphite R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::Maokai:
				x = tab->add_checkbox("MaokaiW", "MaokaiW", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::MonkeyKing:
				x = tab->add_checkbox("MonkeyKingDecoy", "Wukong W", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				x = tab->add_checkbox("MonkeyKingNimbus", "Wukong E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Nidalee:
				x = tab->add_checkbox("Pounce", "Nidalee W", true);
				x->set_texture(spell_icon("Pounce"));
				break;
			case champion_id::Ornn:
				x = tab->add_checkbox("OrnnE", "Ornn E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Pantheon:
				x = tab->add_checkbox("PantheonW", "Pantheon W", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Poppy:
				x = tab->add_checkbox("PoppyE", "Poppy E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Pyke:
				x = tab->add_checkbox("PykeE", "Pyke E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Qiyana:
				x = tab->add_checkbox("QiyanaE", "Qiyana E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Rakan:
				x = tab->add_checkbox("RakanW", "Rakan W", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				break;
			case champion_id::Rammus:
				x = tab->add_checkbox("Tremors2", "Rammus R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::RekSai:
				// this doesnt work when she uses a tunnel that was placed before, since there is no spellcast to process
				x = tab->add_checkbox("RekSaiEBurrowed", "RekSai E Burrowed", true);
				x->set_texture(spell_icon("RekSaiEBurrowed"));
				break;
			case champion_id::Rell:
				x = tab->add_checkbox("RellW_Dismount", "Rell W", true);
				x->set_texture(spell_icon("RellW_Dismount"));
				break;
			case champion_id::Renekton:
				x = tab->add_checkbox("RenektonSliceAndDice", "Renekton E1", true);
				x->set_texture(spell_icon("RenektonSliceAndDice"));
				x = tab->add_checkbox("RenektonDice", "Renekton E2", true);
				x->set_texture(spell_icon("RenektonDice"));
				break;
			case champion_id::Riven:
				x = tab->add_checkbox("RivenFeint", "Riven E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Samira:
				x = tab->add_checkbox("SamiraE", "Samira E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Sejuani:
				x = tab->add_checkbox("SejuaniQ", "Sejuani Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Shen:
				x = tab->add_checkbox("ShenE", "Shen E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Shyvana:
				x = tab->add_checkbox("ShyvanaTransformLeap", "Shyvana R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::Sylas:
				x = tab->add_checkbox("SylasW", "Sylas W", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				x = tab->add_checkbox("SylasE", "Sylas E1", true);
				x->set_texture(spell_icon("SylasE"));
				break;
			case champion_id::Talon:
				x = tab->add_checkbox("TalonQ", "Talon Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Tristana:
				x = tab->add_checkbox("TristanaW", "TristanaW", true);
				x->set_texture(hero->get_spell(spellslot::w)->get_icon_texture());
				break;
			case champion_id::Tryndamere:
				x = tab->add_checkbox("TryndamereE", "Tryndamere E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Urgot:
				x = tab->add_checkbox("UrgotE", "Urgot E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Vayne:
				x = tab->add_checkbox("VayneTumble", "Vayne Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Vi:
				x = tab->add_checkbox("ViQ", "Vi Q", true);
				x->set_texture(hero->get_spell(spellslot::q)->get_icon_texture());
				break;
			case champion_id::Viego:
				x = tab->add_checkbox("ViegoW", "Viego W", true);
				x->set_texture(spell_icon("ViegoW"));
				x = tab->add_checkbox("ViegoR", "Viego R", true);
				x->set_texture(spell_icon("ViegoR"));
				break;
			case champion_id::Volibear:
				x = tab->add_checkbox("VolibearR", "Volibear R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::XinZhao:
				x = tab->add_checkbox("XinZhaoEDash", "XinZhao E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Yasuo:
				x = tab->add_checkbox("YasuoEDash", "Yasuo E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Yone:
				x = tab->add_checkbox("YoneQ3", "Yone Q3", true);
				x->set_texture(spell_icon("YoneQ3"));
				x = tab->add_checkbox("YoneE", "Yone E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Zac:
				x = tab->add_checkbox("ZacE", "Zac E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Zed:
				x = tab->add_checkbox("ZedR", "Zed R", true);
				x->set_texture(hero->get_spell(spellslot::r)->get_icon_texture());
				break;
			case champion_id::Zeri:
				x = tab->add_checkbox("ZeriE", "Zeri E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			case champion_id::Belveth:
				x = tab->add_checkbox("BelvethQ", "Belveth Q", true);
				x->set_texture(spell_icon("BelvethQ",14));	// wow
				break;
			case champion_id::Nilah:
				x = tab->add_checkbox("NilahE", "Nilah E", true);
				x->set_texture(hero->get_spell(spellslot::e)->get_icon_texture());
				break;
			default: break;
			}
		}
	}
}