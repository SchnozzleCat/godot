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

#ifndef Spine_MixPose_h
#define Spine_MixPose_h

namespace spine {

	/// Controls how timeline values are mixed with setup pose values or current pose values when a timeline is applied with
	/// alpha < 1.
	///
	/// See Timeline::apply().
	enum MixBlend {
		/// Transitions between the setup and timeline values (the current value is not used). Before the first frame, the setup
		/// value is used.
		///
		/// MixBlend_Setup is intended to transition to or from the setup pose, not for animations layered on top of others.
		MixBlend_Setup = 0,
		/// Transitions between the current and timeline values. Before the first frame, transitions between the current and setup
		/// values. Timelines which perform instant transitions, such as DrawOrderTimeline or AttachmentTimeline, use
		/// the setup value before the first frame.
		///
		/// MixBlend_First is intended for the first animations applied, not for animations layered on top of others.
		MixBlend_First,
		/// Transitions between the current and timeline values. No change is made before the first frame.
		///
		/// MixBlend_Replace is intended for animations layered on top of others, not for the first animations applied.
		MixBlend_Replace,
		/// Transitions between the current value and the current plus timeline values. No change is made before the first frame.
		///
		/// MixBlend_Add is intended for animations layered on top of others, not for the first animations applied.
		///
		/// Properties set by additive animations must be set manually or by another animation before applying the additive
		/// animations, else the property values will increase each time the additive animations are applied.
		MixBlend_Add
	};
}

#endif /* Spine_MixPose_h */
