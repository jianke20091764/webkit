/*
 * Copyright (C) 2006, 2016 Apple Inc.  All rights reserved.
 * Copyright (C) 2008-2009 Torch Mobile, Inc.
 * Copyright (C) Research In Motion Limited 2009-2010. All rights reserved.
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ImageBackingStore.h"
#include "ImageSource.h"
#include "IntRect.h"
#include "IntSize.h"
#include "PlatformScreen.h"
#include "SharedBuffer.h"
#include <wtf/Assertions.h>
#include <wtf/Optional.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

using ColorProfile = Vector<char>;

    // ImageFrame represents the decoded image data.  This buffer is what all
    // decoders write a single frame into.
    class ImageFrame {
    public:
        enum FrameStatus { FrameEmpty, FramePartial, FrameComplete };
        enum FrameDisposalMethod {
            // If you change the numeric values of these, make sure you audit
            // all users, as some users may cast raw values to/from these
            // constants.
            DisposeNotSpecified,      // Leave frame in framebuffer
            DisposeKeep,              // Leave frame in framebuffer
            DisposeOverwriteBgcolor,  // Clear frame to transparent
            DisposeOverwritePrevious  // Clear frame to previous framebuffer
                                      // contents
        };

        ImageFrame();

        ImageFrame(const ImageFrame& other) { operator=(other); }

        // For backends which refcount their data, this operator doesn't need to
        // create a new copy of the image data, only increase the ref count.
        ImageFrame& operator=(const ImageFrame& other);

        // These do not touch other metadata, only the raw pixel data.
        void clearPixelData();
        void zeroFillPixelData();
        void zeroFillFrameRect(const IntRect&);
        
        // Copies the pixel data at [(startX, startY), (endX, startY)) to the
        // same X-coordinates on each subsequent row up to but not including
        // endY.
        void copyRowNTimes(int startX, int endX, int startY, int endY)
        {
            m_backingStore->repeatFirstRow(IntRect(startX, startY, endX -startX , endY - startY));
        }

        // Makes this frame have an independent copy of the provided image's
        // pixel data, so that modifications in one frame are not reflected in
        // the other. Returns whether the copy succeeded.
        bool initializeBackingStore(const ImageBackingStore&);

        // Allocates space for the pixel data. Returns whether allocation succeeded.
        bool initializeBackingStore(const IntSize&, bool premultiplyAlpha);

        IntSize size() const { return m_backingStore ? m_backingStore->size() : IntSize(); }

        // Returns a caller-owned pointer to the underlying native image data.
        // (Actual use: This pointer will be owned by BitmapImage and freed in
        // FrameData::clear()).
        NativeImagePtr asNewNativeImage() const { return m_backingStore ? m_backingStore->image() : nullptr; }

        inline ImageBackingStore* backingStore() const { return m_backingStore ? m_backingStore.get() : nullptr; }
        inline bool hasBackingStore() const { return backingStore(); }
        
        bool hasAlpha() const;
        IntRect originalFrameRect() const { return m_backingStore ? m_backingStore->frameRect() : IntRect(); }
        FrameStatus status() const { return m_status; }
        unsigned duration() const { return m_duration; }
        FrameDisposalMethod disposalMethod() const { return m_disposalMethod; }

        void setHasAlpha(bool alpha);
        void setOriginalFrameRect(const IntRect&);
        void setStatus(FrameStatus status);
        void setDuration(unsigned duration) { m_duration = duration; }
        void setDisposalMethod(FrameDisposalMethod method) { m_disposalMethod = method; }

        inline RGBA32* pixelAt(int x, int y)
        {
            ASSERT(m_backingStore);
            return m_backingStore->pixelAt(x, y);
        }

        inline void setPixel(int x, int y, unsigned r, unsigned g, unsigned b, unsigned a)
        {
            ASSERT(m_backingStore);
            m_backingStore->setPixel(x, y, r, g, b, a);
        }

        inline void setPixel(RGBA32* dest, unsigned r, unsigned g, unsigned b, unsigned a)
        {
            ASSERT(m_backingStore);
            m_backingStore->setPixel(dest, r, g, b, a);
        }

#if ENABLE(APNG)
        inline void blendPixel(RGBA32* dest, unsigned r, unsigned g, unsigned b, unsigned a)
        {
            ASSERT(m_backingStore);
            m_backingStore->blendPixel(dest, r, g, b, a);
        }
#endif

    private:
        std::unique_ptr<ImageBackingStore> m_backingStore;
        bool m_hasAlpha;
        FrameStatus m_status;
        unsigned m_duration;
        FrameDisposalMethod m_disposalMethod;
    };

    // ImageDecoder is a base for all format-specific decoders
    // (e.g. JPEGImageDecoder).  This base manages the ImageFrame cache.
    //
    // ENABLE(IMAGE_DECODER_DOWN_SAMPLING) allows image decoders to downsample
    // at decode time.  Image decoders will downsample any images larger than
    // |m_maxNumPixels|.  FIXME: Not yet supported by all decoders.
    class ImageDecoder {
        WTF_MAKE_NONCOPYABLE(ImageDecoder); WTF_MAKE_FAST_ALLOCATED;
    public:
        ImageDecoder(ImageSource::AlphaOption alphaOption, ImageSource::GammaAndColorProfileOption gammaAndColorProfileOption)
            : m_premultiplyAlpha(alphaOption == ImageSource::AlphaPremultiplied)
            , m_ignoreGammaAndColorProfile(gammaAndColorProfileOption == ImageSource::GammaAndColorProfileIgnored)
        {
        }

        virtual ~ImageDecoder()
        {
        }

        // Returns a caller-owned decoder of the appropriate type.  Returns 0 if
        // we can't sniff a supported type from the provided data (possibly
        // because there isn't enough data yet).
        static std::unique_ptr<ImageDecoder> create(const SharedBuffer& data, ImageSource::AlphaOption, ImageSource::GammaAndColorProfileOption);

        virtual String filenameExtension() const = 0;
        
        bool premultiplyAlpha() const { return m_premultiplyAlpha; }

        bool isAllDataReceived() const { return m_isAllDataReceived; }

        virtual void setData(SharedBuffer& data, bool allDataReceived)
        {
            if (m_failed)
                return;
            m_data = &data;
            m_isAllDataReceived = allDataReceived;
        }

        // Lazily-decodes enough of the image to get the size (if possible).
        // FIXME: Right now that has to be done by each subclass; factor the
        // decode call out and use it here.
        virtual bool isSizeAvailable()
        {
            return !m_failed && m_sizeAvailable;
        }

        virtual IntSize size() { return isSizeAvailable() ? m_size : IntSize(); }

        IntSize scaledSize()
        {
            return m_scaled ? IntSize(m_scaledColumns.size(), m_scaledRows.size()) : size();
        }

        // This will only differ from size() for ICO (where each frame is a
        // different icon) or other formats where different frames are different
        // sizes.  This does NOT differ from size() for GIF, since decoding GIFs
        // composites any smaller frames against previous frames to create full-
        // size frames.
        virtual IntSize frameSizeAtIndex(size_t, SubsamplingLevel)
        {
            return size();
        }

        // Returns whether the size is legal (i.e. not going to result in
        // overflow elsewhere).  If not, marks decoding as failed.
        virtual bool setSize(const IntSize& size)
        {
            if (ImageBackingStore::isOverSize(size))
                return setFailed();
            m_size = size;
            m_sizeAvailable = true;
            return true;
        }

        // Lazily-decodes enough of the image to get the frame count (if
        // possible), without decoding the individual frames.
        // FIXME: Right now that has to be done by each subclass; factor the
        // decode call out and use it here.
        virtual size_t frameCount() { return 1; }

        virtual int repetitionCount() const { return cAnimationNone; }

        // Decodes as much of the requested frame as possible, and returns an
        // ImageDecoder-owned pointer.
        virtual ImageFrame* frameBufferAtIndex(size_t) = 0;

        bool frameIsCompleteAtIndex(size_t);

        // Make the best effort guess to check if the requested frame has alpha channel.
        bool frameHasAlphaAtIndex(size_t) const;

        // Number of bytes in the decoded frame requested. Return 0 if not yet decoded.
        unsigned frameBytesAtIndex(size_t) const;
        
        float frameDurationAtIndex(size_t);
        
        NativeImagePtr createFrameImageAtIndex(size_t, SubsamplingLevel);

        void setIgnoreGammaAndColorProfile(bool flag) { m_ignoreGammaAndColorProfile = flag; }
        bool ignoresGammaAndColorProfile() const { return m_ignoreGammaAndColorProfile; }

        ImageOrientation orientationAtIndex(size_t) const { return m_orientation; }
        
        bool allowSubsamplingOfFrameAtIndex(size_t) const { return false; }

        enum { iccColorProfileHeaderLength = 128 };

        static bool rgbColorProfile(const char* profileData, unsigned profileLength)
        {
            ASSERT_UNUSED(profileLength, profileLength >= iccColorProfileHeaderLength);

            return !memcmp(&profileData[16], "RGB ", 4);
        }

        static size_t bytesDecodedToDetermineProperties() { return 0; }
        
        static SubsamplingLevel subsamplingLevelForScale(float, SubsamplingLevel) { return 0; }

        static bool inputDeviceColorProfile(const char* profileData, unsigned profileLength)
        {
            ASSERT_UNUSED(profileLength, profileLength >= iccColorProfileHeaderLength);

            return !memcmp(&profileData[12], "mntr", 4) || !memcmp(&profileData[12], "scnr", 4);
        }

        // Sets the "decode failure" flag.  For caller convenience (since so
        // many callers want to return false after calling this), returns false
        // to enable easy tailcalling.  Subclasses may override this to also
        // clean up any local data.
        virtual bool setFailed()
        {
            m_failed = true;
            return false;
        }

        bool failed() const { return m_failed; }

        // Clears decoded pixel data from before the provided frame unless that
        // data may be needed to decode future frames (e.g. due to GIF frame
        // compositing).
        virtual void clearFrameBufferCache(size_t) { }

        // If the image has a cursor hot-spot, stores it in the argument
        // and returns true. Otherwise returns false.
        virtual Optional<IntPoint> hotSpot() const { return Nullopt; }

    protected:
        void prepareScaleDataIfNecessary();
        int upperBoundScaledX(int origX, int searchStart = 0);
        int lowerBoundScaledX(int origX, int searchStart = 0);
        int upperBoundScaledY(int origY, int searchStart = 0);
        int lowerBoundScaledY(int origY, int searchStart = 0);
        int scaledY(int origY, int searchStart = 0);

        RefPtr<SharedBuffer> m_data; // The encoded data.
        Vector<ImageFrame, 1> m_frameBufferCache;
        bool m_scaled { false };
        Vector<int> m_scaledColumns;
        Vector<int> m_scaledRows;
        bool m_premultiplyAlpha;
        bool m_ignoreGammaAndColorProfile;
        ImageOrientation m_orientation;

    private:
        IntSize m_size;
        bool m_sizeAvailable { false };
#if ENABLE(IMAGE_DECODER_DOWN_SAMPLING)
        static const int m_maxNumPixels { 1024 * 1024 };
#else
        static const int m_maxNumPixels { -1 };
#endif
        bool m_isAllDataReceived { false };
        bool m_failed { false };
    };

} // namespace WebCore
