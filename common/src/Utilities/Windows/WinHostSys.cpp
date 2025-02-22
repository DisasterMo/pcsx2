/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "Utilities/RedtapeWindows.h"
#include "PageFaultSource.h"

#include <winnt.h>

static long DoSysPageFaultExceptionFilter(EXCEPTION_POINTERS *eps)
{
    if (eps->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    // Note: This exception can be accessed by the EE or MTVU thread
    // Source_PageFault is a global variable with its own state information
    // so for now we lock this exception code unless someone can fix this better...
    Threading::ScopedLock lock(PageFault_Mutex);
    Source_PageFault->Dispatch(PageFaultInfo((uptr)eps->ExceptionRecord->ExceptionInformation[1]));
    return Source_PageFault->WasHandled() ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

long __stdcall SysPageFaultExceptionFilter(EXCEPTION_POINTERS *eps)
{
    // Prevent recursive exception filtering by catching the exception from the filter here.
    // In the event that the filter causes an access violation (happened during shutdown
    // because Source_PageFault was deallocated), this will allow the debugger to catch the
    // exception.
    // TODO: find a reliable way to debug the filter itself, I've come up with a few ways that
    // work but I don't fully understand why some do and some don't.
    __try {
        return DoSysPageFaultExceptionFilter(eps);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

void _platform_InstallSignalHandler()
{
#ifdef _WIN64 // We don't handle SEH properly on Win64 so use a vectored exception handler instead
    AddVectoredExceptionHandler(true, SysPageFaultExceptionFilter);
#endif
}


static DWORD ConvertToWinApi(const PageProtectionMode &mode)
{
    bool can_write = mode.CanWrite();
    // Windows has some really bizarre memory protection enumeration that uses bitwise
    // numbering (like flags) but is in fact not a flag value.  *Someone* from the early
    // microsoft days wasn't a very good coder, me thinks.  --air
    if      (mode.CanExecute())
        return can_write ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    else if (mode.CanRead())
        return can_write ? PAGE_READWRITE : PAGE_READONLY;
    return PAGE_NOACCESS;
}

void *HostSys::MmapReservePtr(void *base, size_t size)
{
    return VirtualAlloc(base, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool HostSys::MmapCommitPtr(void *base, size_t size, const PageProtectionMode &mode)
{
    void *result = VirtualAlloc(base, size, MEM_COMMIT, ConvertToWinApi(mode));
    if (result)
        return true;

    const DWORD errcode = GetLastError();
    if (errcode == ERROR_COMMITMENT_MINIMUM)
    {
       log_cb(RETRO_LOG_WARN, "(MmapCommit) Received windows error %u {Virtual Memory Minimum Too Low}.\n", ERROR_COMMITMENT_MINIMUM);
       Sleep(1000); // Cut windows some time to rework its memory...
    }
    else if (errcode != ERROR_NOT_ENOUGH_MEMORY && errcode != ERROR_OUTOFMEMORY)
        return false;

    if (!pxDoOutOfMemory)
        return false;
    pxDoOutOfMemory(size);
    return VirtualAlloc(base, size, MEM_COMMIT, ConvertToWinApi(mode)) != NULL;
}

void HostSys::MmapResetPtr(void *base, size_t size)
{
    VirtualFree(base, size, MEM_DECOMMIT);
}


void *HostSys::MmapReserve(uptr base, size_t size)
{
    return MmapReservePtr((void *)base, size);
}

bool HostSys::MmapCommit(uptr base, size_t size, const PageProtectionMode &mode)
{
    return MmapCommitPtr((void *)base, size, mode);
}

void HostSys::MmapReset(uptr base, size_t size)
{
    MmapResetPtr((void *)base, size);
}


void *HostSys::Mmap(uptr base, size_t size)
{
    return VirtualAlloc((void *)base, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

void HostSys::Munmap(uptr base, size_t size)
{
    if (!base)
        return;
    //VirtualFree((void*)base, size, MEM_DECOMMIT);
    VirtualFree((void *)base, 0, MEM_RELEASE);
}

void HostSys::MemProtect(void *baseaddr, size_t size, const PageProtectionMode &mode)
{
    DWORD OldProtect; // enjoy my uselessness, yo!
    if (!VirtualProtect(baseaddr, size, ConvertToWinApi(mode), &OldProtect))
    {
       log_cb(RETRO_LOG_ERROR,
             "VirtualProtect failed @ 0x%08X -> 0x%08X  (mode=%s)\n",
             baseaddr, (uptr)baseaddr + size, mode.ToString().c_str());
    }
}
