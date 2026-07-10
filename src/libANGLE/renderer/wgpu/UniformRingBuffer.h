//
// Copyright 2024 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// UniformRingBuffer.h:
//    A ring buffer for batching uniform data uploads. Instead of creating a new
//    WGPUBuffer per draw call, uniform data is sub-allocated from a single large
//    buffer using dynamic offsets.
//

#ifndef LIBANGLE_RENDERER_WGPU_UNIFORMRINGBUFFER_H_
#define LIBANGLE_RENDERER_WGPU_UNIFORMRINGBUFFER_H_

#include "libANGLE/renderer/wgpu/wgpu_helpers.h"

namespace rx
{
class ContextWgpu;

namespace webgpu
{

// Manages a persistently-mapped GPU buffer from which uniform sub-allocations
// are made per draw call.  At the end of a frame (or when full) the offset is
// reset and the buffer reused.  Combined with dynamic bind group offsets this
// eliminates per-draw buffer creation.
class UniformRingBuffer
{
  public:
    UniformRingBuffer();
    ~UniformRingBuffer();

    // Initialize with a given capacity.  alignment is typically
    // minUniformBufferOffsetAlignment from device limits.
    angle::Result init(ContextWgpu *context, size_t capacity, uint32_t alignment);

    // Sub-allocate |size| bytes (rounded up to alignment).  Returns the byte
    // offset within the buffer.  Caller can then memcpy into getMappedPointer() + offset.
    angle::Result allocate(size_t size, uint32_t *offsetOut);

    // Pointer to the mapped memory of the entire buffer.
    uint8_t *getMappedPointer() const { return mMappedPtr; }

    // The underlying GPU buffer (used in bind group creation).
    const BufferHelper &getBuffer() const { return mBuffer; }
    BufferHelper &getBuffer() { return mBuffer; }

    // Reset the write cursor to 0.  Called after a frame submit.
    void reset();

    // Check whether |size| (after alignment) fits in remaining space.
    bool hasSpace(size_t size) const;

    // Current write offset (for diagnostics / assertions).
    size_t getCurrentOffset() const { return mCurrentOffset; }

    // Total capacity.
    size_t getCapacity() const { return mCapacity; }

  private:
    size_t alignUp(size_t value) const;

    BufferHelper mBuffer;
    size_t mCapacity       = 0;
    size_t mCurrentOffset  = 0;
    uint32_t mMinAlignment = 256;
    uint8_t *mMappedPtr    = nullptr;
};

}  // namespace webgpu
}  // namespace rx

#endif  // LIBANGLE_RENDERER_WGPU_UNIFORMRINGBUFFER_H_
