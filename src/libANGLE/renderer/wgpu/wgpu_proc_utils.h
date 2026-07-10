//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifndef LIBANGLE_RENDERER_WGPU_WGPU_PROC_UTILS_H_
#define LIBANGLE_RENDERER_WGPU_WGPU_PROC_UTILS_H_

struct DawnProcTable;

typedef struct WGPUDeviceImpl *WGPUDevice;
typedef struct WGPUAdapterImpl *WGPUAdapter;
typedef struct WGPUInstanceImpl *WGPUInstance;

namespace rx
{
namespace webgpu
{
const DawnProcTable &GetDefaultProcTable();

struct CreateDeviceSyncResult
{
    WGPUDevice device;
    WGPUAdapter adapter;
    WGPUInstance instance;
    bool success;
};

CreateDeviceSyncResult CreateDeviceSync();

}  // namespace webgpu
}  // namespace rx

#endif  // LIBANGLE_RENDERER_WGPU_WGPU_PROC_UTILS_H_
