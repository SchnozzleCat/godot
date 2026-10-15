/**************************************************************************/
/*  vault_container.cpp                                                   */
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
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "vault_container.h"

#include "core/crypto/crypto_core.h"

// Fixed header offsets (see vault_container.h).
static constexpr size_t OFF_MAGIC = 0;
static constexpr size_t OFF_VERSION = 4;
static constexpr size_t OFF_FLAGS = 6;
static constexpr size_t OFF_UID = 8;
static constexpr size_t OFF_SCHEME = 16;
static constexpr size_t OFF_RESERVED0 = 17;
static constexpr size_t OFF_KEY_TAG = 18;
static constexpr size_t OFF_RESERVED1 = 22;
static constexpr size_t OFF_NONCE = 24;
static constexpr size_t OFF_PLAIN_LENGTH = 36;
static constexpr size_t OFF_GCM_TAG = 44;
static constexpr size_t OFF_PAD = 60;
// Authenticated data is header[0..44) (everything before the GCM tag).
static constexpr size_t AAD_SIZE = OFF_GCM_TAG;

bool VaultContainer::is_binary_resource_payload(const uint8_t p_magic[4]) {
	// "RSRC" = binary resource, "RSCC" = compressed binary resource.
	return (p_magic[0] == 'R' && p_magic[1] == 'S' && p_magic[2] == 'R' && p_magic[3] == 'C') ||
			(p_magic[0] == 'R' && p_magic[1] == 'S' && p_magic[2] == 'C' && p_magic[3] == 'C');
}

bool VaultContainer::is_compressed_texture_payload(const uint8_t p_magic[4]) {
	// "GST2" = CompressedTexture2D, "GSTL" = layered/3D compressed texture
	// (see scene/resources/compressed_texture.cpp).
	return (p_magic[0] == 'G' && p_magic[1] == 'S' && p_magic[2] == 'T') &&
			(p_magic[3] == '2' || p_magic[3] == 'L');
}

static inline void store_u16_le(uint8_t *p_dst, uint16_t p_val) {
	p_dst[0] = (uint8_t)(p_val & 0xFF);
	p_dst[1] = (uint8_t)((p_val >> 8) & 0xFF);
}

static inline uint16_t load_u16_le(const uint8_t *p_src) {
	return (uint16_t)p_src[0] | ((uint16_t)p_src[1] << 8);
}

static inline void store_u32_le(uint8_t *p_dst, uint32_t p_val) {
	p_dst[0] = (uint8_t)(p_val & 0xFF);
	p_dst[1] = (uint8_t)((p_val >> 8) & 0xFF);
	p_dst[2] = (uint8_t)((p_val >> 16) & 0xFF);
	p_dst[3] = (uint8_t)((p_val >> 24) & 0xFF);
}

static inline uint32_t load_u32_le(const uint8_t *p_src) {
	return (uint32_t)p_src[0] | ((uint32_t)p_src[1] << 8) | ((uint32_t)p_src[2] << 16) | ((uint32_t)p_src[3] << 24);
}

static inline void store_u64_le(uint8_t *p_dst, uint64_t p_val) {
	for (int i = 0; i < 8; i++) {
		p_dst[i] = (uint8_t)((p_val >> (8 * i)) & 0xFF);
	}
}

static inline uint64_t load_u64_le(const uint8_t *p_src) {
	uint64_t val = 0;
	for (int i = 7; i >= 0; i--) {
		val = (val << 8) | p_src[i];
	}
	return val;
}

void VaultContainer::_encode_header(uint8_t *p_dst, const Header &p_hdr) {
	p_dst[OFF_MAGIC + 0] = 'G';
	p_dst[OFF_MAGIC + 1] = 'V';
	p_dst[OFF_MAGIC + 2] = 'A';
	p_dst[OFF_MAGIC + 3] = 'U';
	store_u16_le(p_dst + OFF_VERSION, p_hdr.version);
	store_u16_le(p_dst + OFF_FLAGS, p_hdr.flags);
	store_u64_le(p_dst + OFF_UID, (uint64_t)p_hdr.uid);
	p_dst[OFF_SCHEME] = p_hdr.scheme;
	p_dst[OFF_RESERVED0] = 0;
	store_u32_le(p_dst + OFF_KEY_TAG, p_hdr.key_tag);
	store_u16_le(p_dst + OFF_RESERVED1, 0);
	memcpy(p_dst + OFF_NONCE, p_hdr.nonce, NONCE_SIZE);
	store_u64_le(p_dst + OFF_PLAIN_LENGTH, p_hdr.plain_length);
	// GCM tag at OFF_GCM_TAG is written by the caller after encryption.
	memset(p_dst + OFF_PAD, 0, HEADER_SIZE - OFF_PAD);
}

Error VaultContainer::_decode_header(const uint8_t *p_src, size_t p_len, Header &r_hdr) {
	ERR_FAIL_COND_V(p_len < HEADER_SIZE, ERR_FILE_UNRECOGNIZED);
	ERR_FAIL_COND_V(p_src[OFF_MAGIC + 0] != 'G' || p_src[OFF_MAGIC + 1] != 'V' ||
						   p_src[OFF_MAGIC + 2] != 'A' || p_src[OFF_MAGIC + 3] != 'U',
			ERR_FILE_UNRECOGNIZED);

	r_hdr.version = load_u16_le(p_src + OFF_VERSION);
	r_hdr.flags = load_u16_le(p_src + OFF_FLAGS);
	r_hdr.uid = (ResourceUID::ID)load_u64_le(p_src + OFF_UID);
	r_hdr.scheme = p_src[OFF_SCHEME];
	r_hdr.key_tag = load_u32_le(p_src + OFF_KEY_TAG);
	memcpy(r_hdr.nonce, p_src + OFF_NONCE, NONCE_SIZE);
	r_hdr.plain_length = load_u64_le(p_src + OFF_PLAIN_LENGTH);

	ERR_FAIL_COND_V(r_hdr.version != FORMAT_VERSION, ERR_FILE_UNRECOGNIZED);
	ERR_FAIL_COND_V(r_hdr.scheme != SCHEME_AES128_GCM, ERR_FILE_UNRECOGNIZED);
	return OK;
}

Error VaultContainer::encrypt(Vector<uint8_t> &r_out, const Vector<uint8_t> &p_plain,
		ResourceUID::ID p_uid, uint32_t p_key_tag, uint16_t p_flags,
		const uint8_t p_key[KEY_SIZE]) {
	Header hdr;
	hdr.version = FORMAT_VERSION;
	hdr.flags = p_flags;
	hdr.uid = p_uid;
	hdr.scheme = SCHEME_AES128_GCM;
	hdr.key_tag = p_key_tag;
	hdr.plain_length = (uint64_t)p_plain.size();

	CryptoCore::RandomGenerator rng;
	Error err = rng.init();
	ERR_FAIL_COND_V(err != OK, err);
	err = rng.get_random_bytes(hdr.nonce, NONCE_SIZE);
	ERR_FAIL_COND_V(err != OK, err);

	r_out.resize(HEADER_SIZE + p_plain.size());
	uint8_t *dst = r_out.ptrw();
	_encode_header(dst, hdr);

	int ret = CryptoCore::aes_gcm_encrypt(p_key, KEY_SIZE,
			hdr.nonce, NONCE_SIZE,
			dst, AAD_SIZE,
			p_plain.ptr(), p_plain.size(),
			dst + HEADER_SIZE, dst + OFF_GCM_TAG);
	ERR_FAIL_COND_V(ret != OK, FAILED);
	return OK;
}

Error VaultContainer::parse_header(const uint8_t *p_buf, size_t p_len, Header &r_hdr) {
	return _decode_header(p_buf, p_len, r_hdr);
}

Error VaultContainer::decrypt(Vector<uint8_t> &r_plain, const Vector<uint8_t> &p_container,
		const uint8_t p_key[KEY_SIZE]) {
	Header hdr;
	Error err = _decode_header(p_container.ptr(), p_container.size(), hdr);
	ERR_FAIL_COND_V(err != OK, err);

	ERR_FAIL_COND_V((size_t)p_container.size() < HEADER_SIZE + (size_t)hdr.plain_length, ERR_FILE_CORRUPT);
	ERR_FAIL_COND_V((size_t)p_container.size() != HEADER_SIZE + (size_t)hdr.plain_length, ERR_FILE_CORRUPT);

	r_plain.resize(hdr.plain_length);

	const uint8_t *src = p_container.ptr();
	Error ret = CryptoCore::aes_gcm_decrypt(p_key, KEY_SIZE,
			hdr.nonce, NONCE_SIZE,
			src, AAD_SIZE,
			src + OFF_GCM_TAG, TAG_SIZE,
			src + HEADER_SIZE, hdr.plain_length,
			r_plain.ptrw());
	ERR_FAIL_COND_V(ret != OK, ret); // ERR_FILE_CORRUPT on wrong key/key tag/tampering, FAILED otherwise.
	return OK;
}
