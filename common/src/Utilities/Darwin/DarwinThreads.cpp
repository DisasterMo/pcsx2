/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2014  PCSX2 Dev Team
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

#include "../PrecompiledHeader.h"
#include "PersistentThread.h"

#include <unistd.h>

#if !defined(__APPLE__)
#error "DarwinThreads.cpp should only be compiled by projects or makefiles targeted at OSX."
#else

#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/mach_port.h>

// Note: assuming multicore is safer because it forces the interlocked routines to use
// the LOCK prefix.  The prefix works on single core CPUs fine (but is slow), but not
// having the LOCK prefix is very bad indeed.

__forceinline void Threading::Sleep(int ms)
{
    usleep(1000 * ms);
}

// For use in spin/wait loops, acts as a hint to Intel CPUs and should, in theory
// improve performance and reduce cpu power consumption.
__forceinline void Threading::SpinWait()
{
    // If this doesn't compile you can just comment it out (it only serves as a
    // performance hint and isn't required).
    __asm__("pause");
}
#endif
