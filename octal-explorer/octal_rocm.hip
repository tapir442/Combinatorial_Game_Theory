// ROCm (HIP) backend of the GPU block driver extend_device() in octal.c.
//
// The kernel does the same as octal_gpu.comp (Vulkan) and block_scan() (CPU):
// for every heap n = n0 + h of a block and every pair a + b = n - k with
// a < b < n0, mark G[a] ^ G[b] in a bitset of reachable values.  One block of
// 256 threads per heap; the bitset lives in shared memory (LDS).
//
// Unlike the Vulkan backend, the nim-values and bitsets live in device memory
// and are copied explicitly (hipMemcpy), which suits discrete GPUs too.
//
// Build (see the Makefile, target octal_rocm.o):
//     hipcc -O3 -mwavefrontsize64 -fPIC --offload-arch=native -c octal_rocm.hip -o octal_rocm.o
// (wave64 is 1.8x faster than the RDNA default wave32 for this kernel)
// and link it into octal / liboctal.so with -DOCTAL_ROCM -lamdhip64.
#include <hip/hip_runtime.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// same layout as GpuPush in octal.c
struct GpuPush {
    int32_t n0, nwords, t;
    uint32_t km[3];
};

__global__ void __launch_bounds__(256) scan_kernel(const uint16_t *__restrict__ G, uint32_t *__restrict__ marks,
                                                   GpuPush pc) {
    __shared__ uint32_t bits[2048];   // up to 65536 values
    const int h = blockIdx.x, lid = threadIdx.x;
    const int n = pc.n0 + h;
    for (int i = lid; i < pc.nwords; i += 256) bits[i] = 0;
    __syncthreads();
    for (int k = 0; k <= pc.t; k++) {
        if (!((pc.km[k >> 5] >> (k & 31)) & 1u)) continue;
        const int m = n - k;
        const int lo = max(1, m - pc.n0 + 1);   // b = m - a < n0
        const int hi = (m - 1) / 2;             // a < b
        for (int a = lo + lid; a <= hi; a += 256) {
            const uint32_t x = G[a] ^ G[m - a];
            const uint32_t bit = 1u << (x & 31u);
            // read first: most values are already marked, contended atomics are slow
            if (!(bits[x >> 5] & bit)) atomicOr(&bits[x >> 5], bit);
        }
    }
    __syncthreads();
    for (int i = lid; i < pc.nwords; i += 256) marks[(size_t)h * pc.nwords + i] = bits[i];
}

// State of the device and of the running computation (callers hold gpu_lock).
static int state;                // 0 not tried, 1 ready, -1 unavailable
static char dev_name[256];
static uint16_t *dG;             // device: nim-values G[0..to)
static uint32_t *dM, *hM;        // device / pinned host: option bitsets of one block

extern "C" int octal_rocm_dev_init(char *name, int len) {
    if (!state) {
        state = -1;
        int n = 0;
        hipDeviceProp_t p;
        hipFuncAttributes fa;
        // the last check fails if octal_rocm.o has no code object for this GPU
        if (hipGetDeviceCount(&n) == hipSuccess && n > 0 && hipSetDevice(0) == hipSuccess &&
            hipGetDeviceProperties(&p, 0) == hipSuccess &&
            hipFuncGetAttributes(&fa, reinterpret_cast<const void *>(scan_kernel)) == hipSuccess) {
            snprintf(dev_name, sizeof dev_name, "%s (%s)", p.name, p.gcnArchName);
            state = 1;
        }
    }
    if (state < 0) return -1;
    if (name) snprintf(name, (size_t)len, "%s", dev_name);
    return 0;
}

extern "C" void octal_rocm_dev_end(void) {
    if (dG) (void)hipFree(dG);
    if (dM) (void)hipFree(dM);
    if (hM) (void)hipHostFree(hM);
    dG = nullptr;
    dM = hM = nullptr;
}

extern "C" int octal_rocm_dev_begin(const uint16_t *G, long from, long to, size_t mark_bytes) {
    if (hipMalloc(&dG, (size_t)to * sizeof *G) != hipSuccess || hipMalloc(&dM, mark_bytes) != hipSuccess ||
        hipHostMalloc(&hM, mark_bytes, hipHostMallocDefault) != hipSuccess) {
        octal_rocm_dev_end();
        return -3;
    }
    if (hipMemcpy(dG, G, (size_t)from * sizeof *G, hipMemcpyHostToDevice) != hipSuccess) {
        octal_rocm_dev_end();
        return -4;
    }
    return 0;
}

extern "C" const uint32_t *octal_rocm_dev_scan(const GpuPush *pc, long B) {
    hipLaunchKernelGGL(scan_kernel, dim3((unsigned)B), dim3(256), 0, 0, dG, dM, *pc);
    if (hipGetLastError() != hipSuccess) return nullptr;
    // synchronous on the default stream: waits for the kernel
    if (hipMemcpy(hM, dM, (size_t)B * pc->nwords * sizeof *hM, hipMemcpyDeviceToHost) != hipSuccess)
        return nullptr;
    return hM;
}

extern "C" int octal_rocm_dev_append(const uint16_t *G, long n0, long B) {
    return hipMemcpy(dG + n0, G + n0, (size_t)B * sizeof *G, hipMemcpyHostToDevice) == hipSuccess ? 0 : -4;
}
