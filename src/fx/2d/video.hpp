/// @brief Implements a video playback effect for 2D LED grids.

#pragma once

#include "FastLED.h"
#include "fx/video/pixel_stream.h"
#include "fx/fx2d.h"
#include "fx/video/frame_interpolator.h"
#include "fl/ptr.h"

FASTLED_NAMESPACE_BEGIN



FASTLED_SMART_PTR(VideoFx);



#if 0

FASTLED_SMART_PTR(VideoFx);

// Converts a FxGrid to a video effect. This primarily allows for
// fixed frame rates and frame interpolation.
class VideoFx : public FxGrid {
  public:
    VideoFx(XYMap xymap) : FxGrid(xymap) {}

    void begin(uint32_t now, FxGridPtr fx,uint16_t nFrameHistory, float fps = -1) {
        mDelegate = fx;
        if (!mDelegate) {
            return; // Early return if delegate is null
        }
        mDelegate->getXYMap().setRectangularGrid();
        mFps = fps < 0 ? 30 : fps;
        mDelegate->hasFixedFrameRate(&mFps);
        mFrameInterpolator = FrameInterpolatorPtr::New(nFrameHistory, mFps);
        mFrameInterpolator->reset(now);
    }

    void lazyInit() override {
        if (!mInitialized) {
            mInitialized = true;
            mDelegate->lazyInit();
        }
    }

    void draw(DrawContext context) override {
        if (!mDelegate) {
            return;
        }

        uint32_t precise_timestamp;
        if (mFrameInterpolator->needsFrame(context.now, &precise_timestamp)) {
            FramePtr frame;
            bool wasFullBeforePop = mFrameInterpolator->full();
            if (wasFullBeforePop) {
                if (!mFrameInterpolator->popOldest(&frame)) {
                    return; // Failed to pop, something went wrong
                }
                if (mFrameInterpolator->full()) {
                    return; // Still full after popping, something went wrong
                }
            } else {
                frame = FramePtr::New(mDelegate->getNumLeds(), mDelegate->hasAlphaChannel());
            }

            if (!frame) {
                return; // Something went wrong.
            }

            DrawContext delegateContext = context;
            delegateContext.leds = frame->rgb();
            delegateContext.alpha_channel = frame->alpha();
            delegateContext.now = precise_timestamp;
            mDelegate->draw(delegateContext);

            mFrameInterpolator->pushNewest(frame, precise_timestamp);
            mFrameInterpolator->incrementFrameCounter();
        }

        mFrameInterpolator->draw(context.now, context.leds, context.alpha_channel);
    }

    const char *fxName(int) const override { return "video_fx"; }

  private:
    Ptr<FxGrid> mDelegate;
    bool mInitialized = false;
    FrameInterpolatorPtr mFrameInterpolator;
    float mFps;
};

#endif

FASTLED_NAMESPACE_END
