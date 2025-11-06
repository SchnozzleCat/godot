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

#ifndef Spine_Animation_h
#define Spine_Animation_h

#include <spine/Array.h>
#include <spine/HashMap.h>
#include <spine/MixBlend.h>
#include <spine/MixDirection.h>
#include <spine/SpineObject.h>
#include <spine/SpineString.h>
#include <spine/Property.h>

namespace spine {
	class Timeline;
	class BoneTimeline;

	class Skeleton;

	class Event;

	class AnimationState;

	/// Stores a list of timelines to animate a skeleton's pose over time.
	class SP_API Animation : public SpineObject {
		friend class AnimationState;

		friend class TrackEntry;

		friend class AnimationStateData;

		friend class AttachmentTimeline;

		friend class RGBATimeline;

		friend class RGBTimeline;

		friend class AlphaTimeline;

		friend class RGBA2Timeline;

		friend class RGB2Timeline;

		friend class DeformTimeline;

		friend class DrawOrderTimeline;

		friend class EventTimeline;

		friend class IkConstraintTimeline;

		friend class PathConstraintMixTimeline;

		friend class PathConstraintPositionTimeline;

		friend class PathConstraintSpacingTimeline;

		friend class RotateTimeline;

		friend class ScaleTimeline;

		friend class ShearTimeline;

		friend class TransformConstraintTimeline;

		friend class TranslateTimeline;

		friend class TranslateXTimeline;

		friend class TranslateYTimeline;

		friend class TwoColorTimeline;

		friend class Slider;

	public:
		Animation(const String &name, Array<Timeline *> &timelines, float duration);

		~Animation();

		/// If the returned array or the timelines it contains are modified, setTimelines() must be called.
		Array<Timeline *> &getTimelines();

		void setTimelines(Array<Timeline *> &timelines);

		/// Returns true if this animation contains a timeline with any of the specified property IDs.
		bool hasTimeline(Array<PropertyId> &ids);

		/// The duration of the animation in seconds, which is usually the highest time of all frames in the timeline. The duration is
		/// used to know when it has completed and when it should loop back to the start.
		float getDuration();

		void setDuration(float inValue);

		/// Applies the animation's timelines to the specified skeleton.
		///
		/// See Timeline::apply().
		/// @param skeleton The skeleton the animation is being applied to. This provides access to the bones, slots, and other skeleton
		///           components the timelines may change.
		/// @param lastTime The last time in seconds this animation was applied. Some timelines trigger only at specific times rather
		///           than every frame. Pass -1 the first time an animation is applied to ensure frame 0 is triggered.
		/// @param time The time in seconds the skeleton is being posed for. Most timelines find the frame before and the frame after
		///           this time and interpolate between the frame values. If beyond the getDuration() and loop is
		///           true then the animation will repeat, else the last frame will be applied.
		/// @param loop If true, the animation repeats after the getDuration().
		/// @param events If any events are fired, they are added to this list. Can be null to ignore fired events or if no timelines
		///           fire events.
		/// @param alpha 0 applies the current or setup values (depending on blend). 1 applies the timeline values. Between
		///           0 and 1 applies values between the current or setup values and the timeline values. By adjusting
		///           alpha over time, an animation can be mixed in or out. alpha can also be useful to apply
		///           animations on top of each other (layering).
		/// @param blend Controls how mixing is applied when alpha < 1.
		/// @param direction Indicates whether the timelines are mixing in or out. Used by timelines which perform instant transitions,
		///           such as DrawOrderTimeline or AttachmentTimeline.
		void apply(Skeleton &skeleton, float lastTime, float time, bool loop, Array<Event *> *events, float alpha, MixBlend blend,
				   MixDirection direction, bool appliedPose);

		/// The animation's name, which is unique across all animations in the skeleton.
		const String &getName();

		/// The bone indices affected by this animation.
		const Array<int> &getBones();

		/// @param target After the first and before the last entry.
		static int search(Array<float> &values, float target);

		static int search(Array<float> &values, float target, int step);

	protected:
		Array<Timeline *> _timelines;
		HashMap<PropertyId, bool> _timelineIds;
		Array<int> _bones;
		float _duration;
		String _name;
	};
}

#endif /* Spine_Animation_h */
