//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "libANGLE/renderer/wgpu/wgpu_proc_utils.h"

// DawnNative.h includes the webgpu cpp API which depends on wgpu function declarations. To hide the
// declarations from the rest of the ANGLE WebGPU backend, only enable them in this file.
#undef WGPU_SKIP_DECLARATIONS
#include <dawn/native/DawnNative.h>
#include <dawn/dawn_proc.h>

namespace rx
{
namespace webgpu
{
const DawnProcTable &GetDefaultProcTable()
{
    return dawn::native::GetProcs();
}

CreateDeviceSyncResult CreateDeviceSync()
{
    CreateDeviceSyncResult result = {};
    result.success = false;

    // Enable TimedWaitAny so ANGLE can wait on shader compilation and other async ops
    static constexpr wgpu::InstanceFeatureName kFeatures[] = {
        wgpu::InstanceFeatureName::TimedWaitAny,
    };
    wgpu::InstanceDescriptor instanceDesc = {};
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = kFeatures;

    dawn::native::Instance nativeInstance(
        reinterpret_cast<const WGPUInstanceDescriptor *>(&instanceDesc));
    auto adapters = nativeInstance.EnumerateAdapters();
    if (adapters.empty())
    {
        return result;
    }
    dawn::native::Adapter selectedAdapter = adapters[0];

    const DawnProcTable &procs = dawn::native::GetProcs();
    dawnProcSetProcs(&procs);
    result.device = selectedAdapter.CreateDevice();
    if (!result.device)
    {
        return result;
    }

    result.adapter = procs.deviceGetAdapter(result.device);
    result.instance = procs.adapterGetInstance(result.adapter);
    result.success = true;
    return result;
}

}  // namespace webgpu
}  // namespace rx
