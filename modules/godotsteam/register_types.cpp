#include "register_types.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/config/engine.h"
#include "godotsteam.h"

static Steam* SteamPtr = NULL;

void initialize_godotsteam_module(ModuleInitializationLevel level){
	if(level == MODULE_INITIALIZATION_LEVEL_SERVERS){
		GDREGISTER_CLASS(Steam);
		SteamPtr = memnew(Steam);
		Engine::get_singleton()->add_singleton(Engine::Singleton("Steam", Steam::get_singleton()));

		// Setup Project Settings
		int app_id = GLOBAL_DEF_BASIC("godot_steam/steam/app_id", 0);
		bool auto_init = GLOBAL_DEF_BASIC("godot_steam/steam/auto_init_steam_on_startup", false);

		// Prevent steamInit from the editor itself
		if (Engine::get_singleton()->is_editor_hint()) {
			return;
		}

		if (!auto_init) {
			return;
		}

		Steam::get_singleton()->steamInitEx(false, app_id, false);
	}
}


void uninitialize_godotsteam_module(ModuleInitializationLevel level){
	if(level == MODULE_INITIALIZATION_LEVEL_SERVERS){
		Engine::get_singleton()->remove_singleton("Steam");
		memdelete(SteamPtr);
	}
}
