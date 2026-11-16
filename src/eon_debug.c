#include "global.h"
#include "eon_debug.h"

#if EON_DEBUG_MEMORY_LOG

#include <stdarg.h>
#include "mini_printf.h"

// mGBA debug console registers (see include/gba/isagbprint.h).
#define REG_DEBUG_ENABLE ((vu16 *)0x04FFF780)
#define REG_DEBUG_FLAGS  ((vu16 *)0x04FFF700)
#define REG_DEBUG_STRING ((char *)0x04FFF600)

static bool8 sEonDebugReady = FALSE;
static bool8 sEonDebugChecked = FALSE;

static void EonDebugInit(void)
{
    if (sEonDebugChecked)
        return;

    sEonDebugChecked = TRUE;
    *REG_DEBUG_ENABLE = 0xC0DE;
    sEonDebugReady = (*REG_DEBUG_ENABLE == 0x1DEA);
}

void EonLog(const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    u32 i;

    EonDebugInit();
    if (!sEonDebugReady)
        return;

    va_start(args, fmt);
    mini_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    for (i = 0; i < sizeof(buffer) - 1 && buffer[i] != '\0'; i++)
        REG_DEBUG_STRING[i] = buffer[i];
    REG_DEBUG_STRING[i] = '\0';

    *REG_DEBUG_FLAGS = 1 | 0x100; // MGBA_LOG_ERROR (visible even with INFO filtered)
}

bool32 EonIsValidPointer(const void *ptr)
{
    u32 value = (u32)ptr;

    return (value < 0x00004000)                       // BIOS
        || (value >= 0x02000000 && value < 0x02040000)   // EWRAM
        || (value >= 0x03000000 && value < 0x03008000)   // IWRAM
        || (value >= 0x04000000 && value < 0x04000400)   // I/O
        || (value >= 0x05000000 && value < 0x05000400)   // Palette RAM
        || (value >= 0x06000000 && value < 0x06018000)   // VRAM
        || (value >= 0x07000000 && value < 0x07000400)   // OAM
        || (value >= 0x08000000 && value < 0x0E000000)   // ROM and mirrors
        || (value >= 0x0E000000 && value < 0x0E010000);  // Save flash window
}

void EonDmaCheck(u32 dmaNum, const void *src, void *dest, u32 size, u32 bit)
{
    if (size > 0x10000
     || (size != 0 && (!EonIsValidPointer(src) || !EonIsValidPointer(dest))))
    {
        EonLog("DMA BAD dma=%u src=%08X dst=%08X size=%u bit=%u lr=%08X",
               dmaNum,
               (u32)src,
               (u32)dest,
               size,
               bit,
               (u32)__builtin_return_address(0));
    }
}

void EonCpuCopyCheck(const void *src, void *dest, u32 size, u32 bit)
{
    if (size > 0x10000
     || (size != 0 && (!EonIsValidPointer(src) || !EonIsValidPointer(dest))))
    {
        EonLog("CPUCOPY BAD src=%08X dst=%08X size=%u bit=%u lr=%08X",
               (u32)src,
               (u32)dest,
               size,
               bit,
               (u32)__builtin_return_address(0));
    }
}

#endif // EON_DEBUG_MEMORY_LOG
