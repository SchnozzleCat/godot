/**************************************************************************/
/*  editor_vault.h                                                        */
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

#ifndef EDITOR_VAULT_H
#define EDITOR_VAULT_H

#include "core/error/error_list.h"
#include "core/io/resource_uid.h"
#include "core/string/ustring.h"

// Editor-side helpers for the Asset Vault ".vault" sidecar markers
// (one per encrypted asset, next to the source — like ".uid" sidecars).
//
// The sidecar is a plain ConfigFile:
//
//   [vault]
//   version=1
//   uid="uid://b3l4r8xqnld0n"
//   tag=1
//
// "tag" is the per-asset key tag (starts at 1 when encryption is enabled,
// bumped by "Rotate Encryption Key"): the per-asset AES-128 key is derived
// at export as HKDF(master, "uid://<uid-text>:<tag>"). External tools can
// build the server catalog by walking the project tree for these sidecars
// (or by scanning the exported pck's GVAU headers, which embed the tag).
class EditorVault {
public:
	static String get_sidecar_path(const String &p_asset_path); // p_asset_path + ".vault"
	static bool is_encrypted(const String &p_asset_path); // Sidecar exists.
	static uint32_t get_key_tag(const String &p_asset_path, uint32_t p_default = 1);

	// Resolves the asset's uid (INVALID_ID if the asset has no uid).
	static ResourceUID::ID get_asset_uid(const String &p_asset_path);
	// The in-pack path the asset is exported to: "res://.vault/<uid-text>.enc".
	static String get_vault_export_path(const String &p_asset_path);

	// Sidecar lifecycle (used by the FileSystem dock context menu).
	// p_uid must be a valid uid for the asset (INVALID_ID fails).
	static Error enable(const String &p_asset_path, ResourceUID::ID p_uid); // Writes sidecar (tag=1).
	static Error disable(const String &p_asset_path); // Removes sidecar.
	static Error rotate_key(const String &p_asset_path); // Bumps tag (re-encrypts nothing locally; affects future exports only).
};

#endif // EDITOR_VAULT_H
