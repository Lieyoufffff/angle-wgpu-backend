#include <cstdio>

#undef WGPU_SKIP_DECLARATIONS
#include <dawn/native/DawnNative.h>
#include <dawn/dawn_proc.h>

int main() {
    fprintf(stderr, "Starting device sync test...\n");
    fflush(stderr);

    dawn::native::Instance nativeInstance;
    fprintf(stderr, "Enumerating adapters...\n");
    fflush(stderr);

    auto adapters = nativeInstance.EnumerateAdapters();
    fprintf(stderr, "Found %zu adapter(s)\n", adapters.size());
    fflush(stderr);

    if (adapters.empty()) {
        fprintf(stderr, "No adapters found!\n");
        return 1;
    }

    dawn::native::Adapter selectedAdapter = adapters[0];

    const DawnProcTable &procs = dawn::native::GetProcs();
    dawnProcSetProcs(&procs);

    fprintf(stderr, "Creating device...\n");
    fflush(stderr);

    WGPUDevice device = selectedAdapter.CreateDevice();
    if (!device) {
        fprintf(stderr, "CreateDevice returned null!\n");
        return 1;
    }

    fprintf(stderr, "Device created successfully!\n");
    fflush(stderr);

    procs.deviceRelease(device);
    return 0;
}
