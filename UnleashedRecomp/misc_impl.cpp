#include "stdafx.h"
#include <kernel/function.h>
#include <kernel/xdm.h>

uint32_t QueryPerformanceCounterImpl(LARGE_INTEGER* lpPerformanceCount)
{
    lpPerformanceCount->QuadPart = ByteSwap(std::chrono::steady_clock::now().time_since_epoch().count());
    return TRUE;
}

uint32_t QueryPerformanceFrequencyImpl(LARGE_INTEGER* lpFrequency)
{
    constexpr auto Frequency = std::chrono::steady_clock::period::den / std::chrono::steady_clock::period::num;
    lpFrequency->QuadPart = ByteSwap(Frequency);
    return TRUE;
}

uint32_t GetTickCountImpl()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void GlobalMemoryStatusImpl(XLPMEMORYSTATUS lpMemoryStatus)
{
    lpMemoryStatus->dwLength = sizeof(XMEMORYSTATUS);
    lpMemoryStatus->dwMemoryLoad = 0;
    lpMemoryStatus->dwTotalPhys = 0x20000000;
    lpMemoryStatus->dwAvailPhys = 0x20000000;
    lpMemoryStatus->dwTotalPageFile = 0x20000000;
    lpMemoryStatus->dwAvailPageFile = 0x20000000;
    lpMemoryStatus->dwTotalVirtual = 0x20000000;
    lpMemoryStatus->dwAvailVirtual = 0x20000000;
}

// The game's copy routines are forward copies, and guest code relies on that when
// the ranges overlap with dst < src: the game's own memmove (sub_831B5E00) calls its
// memcpy (sub_831B0ED0) for that case, and Havok's broadphase shifts its endpoint
// arrays down in place with sub_831B1358. Host memcpy is undefined for overlapping
// ranges, and some Android libcs do corrupt them, so every copy routine maps to
// memmove.
GUEST_FUNCTION_HOOK(sub_831B0ED0, memmove);
GUEST_FUNCTION_HOOK(sub_831CCB98, memmove);
GUEST_FUNCTION_HOOK(sub_831CEAE0, memmove);
GUEST_FUNCTION_HOOK(sub_831CEE04, memmove);
GUEST_FUNCTION_HOOK(sub_831CF2D0, memmove);
GUEST_FUNCTION_HOOK(sub_831CF660, memmove);
GUEST_FUNCTION_HOOK(sub_831B1358, memmove);
GUEST_FUNCTION_HOOK(sub_831B5E00, memmove);
GUEST_FUNCTION_HOOK(sub_831B0BA0, memset);
GUEST_FUNCTION_HOOK(sub_831CCAA0, memset);

#ifdef _WIN32
GUEST_FUNCTION_HOOK(sub_82BD4CA8, OutputDebugStringA);
#else
GUEST_FUNCTION_STUB(sub_82BD4CA8);
#endif

GUEST_FUNCTION_HOOK(sub_82BD4AC8, QueryPerformanceCounterImpl);
GUEST_FUNCTION_HOOK(sub_831CD040, QueryPerformanceFrequencyImpl);
GUEST_FUNCTION_HOOK(sub_831CDAD0, GetTickCountImpl);

GUEST_FUNCTION_HOOK(sub_82BD4BC0, GlobalMemoryStatusImpl);

// sprintf
PPC_FUNC(sub_82BD4AE8)
{
    sub_831B1630(ctx, base);
}
