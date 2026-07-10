//
// Copyright 2024 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// UniformRingBuffer.cpp:
//    Implementation of the uniform ring buffer for batched uploads.
//

#include "libANGLE/renderer/wgpu/UniformRingBuffer.h"

#include "libANGLE/renderer/wgpu/ContextWgpu.h"

namespace rx
{
namespace webgpu
{

UniformRingBuffer::UniformRingBuffer() = default;

UniformRingBuffer::~UniformRingBuffer() = default;

angle::Result UniformRingBuffer::init(ContextWgpu *context,
                                      size_t capacity,
                                      uint32_t alignment)
{
    mCapacity      = capacity;
    mMinAlignment  = alignment;
    mCurrentOffset = 0;

    // Create a single large buffer with Uniform | CopyDst usage, mapped at creation
    // so we can write sub-allocations directly.
    const DawnProcTable *wgpu = webgpu::GetProcs(context);
    ANGLE_TRY(mBuffer.initBuffer(
        wgpu, context->getDevice(), capacity,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        MapAtCreation::Yes));

    mMappedPtr = static_cast<uint8_t *>(mBuffer.getMapWritePointer(0, capacity));
    ASSERT(mMappedPtr != nullptr);

    return angle::Result::Continue;
}

angle::Result UniformRingBuffer::allocate(size_t size, uint32_t *offsetOut)
{
    size_t alignedSize = alignUp(size);

    // If we ran out of space, this is an error condition.
    // The caller should check hasSpace() before calling allocate().
    ASSERT(mCurrentOffset + alignedSize <= mCapacity);

    *offsetOut = static_cast<uint32_t>(mCurrentOffset);
    mCurrentOffset += alignedSize;

    return angle::Result::Continue;
}

void UniformRingBuffer::reset()
{
    mCurrentOffset = 0;
}

bool UniformRingBuffer::hasSpace(size_t size) const
{
    return (mCurrentOffset + alignUp(size)) <= mCapacity;
}

size_t UniformRingBuffer::alignUp(size_t value) const
{
    size_t mask = static_cast<size_t>(mMinAlignment) - 1;
    return (value + mask) & ~mask;
}

}  // namespace webgpu
}  // namespace rx
