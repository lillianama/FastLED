#include "fx/video/frame_interpolator.h"
#include "fl/circular_buffer.h"
#include "fx/video/pixel_stream.h"
#include "fl/math_macros.h"
#include "namespace.h"

#include "fl/dbg.h"

#include "fl/math_macros.h"
#include <math.h>

#define DBG FASTLED_DBG


FASTLED_NAMESPACE_BEGIN

FrameInterpolator::FrameInterpolator(size_t nframes, float fps)
    : mFrames(MAX(1, nframes)), mFrameTracker(fps) {
}

bool FrameInterpolator::draw(uint32_t now, Frame *dst) {
    // DBG("FrameInterpolator::draw");
    bool ok = draw(now, dst->rgb(), dst->alpha());
    if (ok) {
        // dst->setTimestamp(now);
    }
    return ok;
}

bool FrameInterpolator::draw(uint32_t now, CRGB* leds, uint8_t* alpha) {
    uint32_t frameNumber, nextFrameNumber;
    uint8_t amountOfNextFrame;
    // DBG("now: " << now);
    mFrameTracker.get_interval_frames(now, &frameNumber, &nextFrameNumber, &amountOfNextFrame);
    if (!has(frameNumber)) {
        return false;
    }

    if (has(frameNumber) && !has(nextFrameNumber)) {
        // just paint the current frame
        Frame* frame = get(frameNumber).get();
        frame->draw(leds, alpha);
        return true;
    }

    Frame* frame1 = get(frameNumber).get();
    Frame* frame2 = get(nextFrameNumber).get();

    Frame::interpolate(*frame1, *frame2, amountOfNextFrame, leds, alpha);
    // DBG("Interpolated frame " << frameNumber << " and " << nextFrameNumber);
    return true;
}

FASTLED_NAMESPACE_END
