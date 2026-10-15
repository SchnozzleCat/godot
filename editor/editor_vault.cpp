/**************************************************************************/
/*  editor_vault.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "editor_vault.h"

#include "core/io/asset_vault.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"

String EditorVault::get_sidecar_path(const String &p_asset_path) {
	return p_asset_path + ".vault";
}

bool EditorVault::is_encrypted(const String &p_asset_path) {
	return FileAccess::exists(get_sidecar_path(p_asset_path));
}

uint32_t EditorVault::get_key_tag(const String &p_asset_path, uint32_t p_default) {
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(get_sidecar_path(p_asset_path)) != OK) {
		return p_default;
	}
	// Missing "tag" in old/migrated sidecars: default (1).
	return (uint32_t)config->get_value("vault", "tag", (int64_t)p_default);
}

ResourceUID::ID EditorVault::get_asset_uid(const String &p_asset_path) {
	// Note: ResourceUID::get_path_id() cannot be used here, it relies on the
	// reverse path cache which is only enabled in running games (not in the
	// editor, incl. headless export). Ask the resource loaders instead:
	// they read the uid embedded in text/binary resources or their .uid
	// sidecar, without needing any cache.
	return ResourceLoader::get_resource_uid(p_asset_path);
}

String EditorVault::get_vault_export_path(const String &p_asset_path) {
	ResourceUID::ID uid = get_asset_uid(p_asset_path);
	if (uid == ResourceUID::INVALID_ID) {
		return String();
	}
	return AssetVault::get_vault_path(uid);
}

Error EditorVault::enable(const String &p_asset_path, ResourceUID::ID p_uid) {
	ERR_FAIL_COND_V_MSG(p_uid == ResourceUID::INVALID_ID, ERR_INVALID_PARAMETER,
			"Vault: cannot encrypt an asset without a UID: " + p_asset_path);

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("vault", "version", 1);
	config->set_value("vault", "uid", ResourceUID::get_singleton()->id_to_text(p_uid));
	// Rotation starts at 1; "Rotate Encryption Key" bumps it.
	config->set_value("vault", "tag", (int64_t)1);
	return config->save(get_sidecar_path(p_asset_path));
}

Error EditorVault::disable(const String &p_asset_path) {
	String sidecar = get_sidecar_path(p_asset_path);
	if (!FileAccess::exists(sidecar)) {
		return ERR_DOES_NOT_EXIST;
	}
	Error err = DirAccess::remove_absolute(sidecar);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Vault: cannot remove sidecar: " + sidecar);
	return OK;
}

Error EditorVault::rotate_key(const String &p_asset_path) {
	String sidecar = get_sidecar_path(p_asset_path);
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(sidecar) != OK) {
		return ERR_FILE_NOT_FOUND;
	}
	uint32_t tag = (uint32_t)config->get_value("vault", "tag", (int64_t)1);
	config->set_value("vault", "tag", (int64_t)(tag + 1));
	return config->save(sidecar);
}
