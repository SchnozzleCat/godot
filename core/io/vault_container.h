/**************************************************************************/
/*  vault_container.h                                                     */
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

#ifndef VAULT_CONTAINER_H
#define VAULT_CONTAINER_H

#include "core/error/error_list.h"
#include "core/io/resource_uid.h"
#include "core/templates/vector.h"

// Container format for vault-encrypted assets ("GVAU").
//
// Layout (all integers little-endian):
//
//   Offset  Size  Field
//   0       4     magic "GVAU" (bytes 'G', 'V', 'A', 'U')
//   4       2     version (1)
//   6       2     flags (bit0: payload is an imported artifact; rest reserved)
//   8       8     uid (ResourceUID::ID, int64)
//   16      1     scheme (1 = AES-128-GCM)
//   17      1     reserved (0)
//   18      4     key tag (starts at 1; bumped by "Rotate Encryption Key")
//   22      2     reserved (0)
//   24      12    nonce (random per file)
//   36      8     plaintext length
//   44      16    GCM tag
//   60      4     reserved (0; pads the header to 64 bytes)
//   64      ...   ciphertext (exactly `plaintext length` bytes; GCM is stream-like,
//                             no padding)
//
// The header is plaintext so external tools can build the server catalog by
// scanning exported files (magic + uid + key tag + scheme at fixed offsets).
// It deliberately does NOT contain the file type; the type lives inside the
// encrypted payload. The GCM tag authenticates header[0..44) || ciphertext,
// binding the uid, flags and key tag to the payload.

class VaultContainer {
public:
	static constexpr uint32_t MAGIC = 0x47564155; // "GVAU"
	static constexpr uint16_t FORMAT_VERSION = 1;
	static constexpr size_t HEADER_SIZE = 64;
	static constexpr size_t KEY_SIZE = 16; // AES-128
	static constexpr size_t NONCE_SIZE = 12;
	static constexpr size_t TAG_SIZE = 16; // GCM

	static constexpr uint8_t SCHEME_AES128_GCM = 1;

	static constexpr uint16_t FLAG_IMPORTED_ARTIFACT = 1 << 0;

	// Supported payload formats (first 4 bytes of the decrypted payload). The
	// uniform ".enc" vault path hides the file type, so loading dispatches on
	// these magics instead of extensions:
	// - "RSRC"/"RSCC": (compressed) generic binary resource, delegated to
	//   ResourceFormatLoaderBinary by the vault loader. Most import artifacts
	//   (.res, .scn, .sample, .translation, .fontdata, ...) use it.
	// - "GST2": CompressedTexture2D; "GSTL": layered/3D compressed textures
	//   (the layer mode in the GSTL header picks the exact class). Declined by
	//   the vault loader and claimed by the compressed texture loaders instead
	//   (see compressed_texture_resource_format.cpp).
	static bool is_binary_resource_payload(const uint8_t p_magic[4]); // "RSRC"/"RSCC".
	static bool is_compressed_texture_payload(const uint8_t p_magic[4]); // "GST2"/"GSTL".

	struct Header {
		uint16_t version = 0;
		uint16_t flags = 0;
		ResourceUID::ID uid = ResourceUID::INVALID_ID;
		uint8_t scheme = 0;
		uint32_t key_tag = 0;
		uint8_t nonce[NONCE_SIZE] = {};
		uint64_t plain_length = 0;
	};

	// Encrypts p_plain with AES-128-GCM (random nonce) into r_out as a GVAU
	// container. p_uid, p_key_tag and p_flags are embedded in the header.
	static Error encrypt(Vector<uint8_t> &r_out, const Vector<uint8_t> &p_plain,
			ResourceUID::ID p_uid, uint32_t p_key_tag, uint16_t p_flags,
			const uint8_t p_key[KEY_SIZE]);

	// Parses the plaintext header (no key required). For scanners.
	static Error parse_header(const uint8_t *p_buf, size_t p_len, Header &r_hdr);

	// Decrypts a GVAU container, verifying the GCM tag. Returns
	// ERR_FILE_CORRUPT on tag mismatch (wrong key, wrong key tag or tampering).
	static Error decrypt(Vector<uint8_t> &r_plain, const Vector<uint8_t> &p_container,
			const uint8_t p_key[KEY_SIZE]);

private:
	static void _encode_header(uint8_t *p_dst, const Header &p_hdr);
	static Error _decode_header(const uint8_t *p_src, size_t p_len, Header &r_hdr);
};

#endif // VAULT_CONTAINER_H
