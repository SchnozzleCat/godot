/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated April 5, 2025. Replaces all prior versions.
 *
 * Copyright (c) 2013-2025, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include <spine/EventData.h>

#include <assert.h>

using namespace spine;

EventData::EventData(const String &name) : _name(name), _intValue(0), _floatValue(0), _stringValue(), _audioPath(), _volume(0), _balance(0) {
	assert(_name.length() > 0);
}

/// The name of the event, which is unique within the skeleton.
const String &EventData::getName() const {
	return _name;
}

int EventData::getInt() const {
	return _intValue;
}

void EventData::setInt(int inValue) {
	_intValue = inValue;
}

float EventData::getFloat() const {
	return _floatValue;
}

void EventData::setFloat(float inValue) {
	_floatValue = inValue;
}

const String &EventData::getString() const {
	return _stringValue;
}

void EventData::setString(const String &inValue) {
	this->_stringValue = inValue;
}

const String &EventData::getAudioPath() const {
	return _audioPath;
}

void EventData::setAudioPath(const String &inValue) {
	_audioPath = inValue;
}


float EventData::getVolume() const {
	return _volume;
}

void EventData::setVolume(float inValue) {
	_volume = inValue;
}

float EventData::getBalance() const {
	return _balance;
}

void EventData::setBalance(float inValue) {
	_balance = inValue;
}
