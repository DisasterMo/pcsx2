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

#include "System.h"
#include "SysThreads.h"

// --------------------------------------------------------------------------------------
//  SysFakeThread *External Thread* Implementations
//    (Called form outside the context of this thread)
// --------------------------------------------------------------------------------------

SysFakeThread::SysFakeThread() :
	m_ExecMode( ExecMode_NoThreadYet )
,	m_ExecModeMutex()
	,  m_thread{}
{
}

void SysFakeThread::Start()
{
//	Sleep( 1 );
	m_running = true;
//	m_sem_event.Post();
	OnStart();
}

void SysFakeThread::OnStart()
{
	if( !pxAssertDev( m_ExecMode == ExecMode_NoThreadYet, "SysFakeThread:Start(): Invalid execution mode" ) ) return;

	m_sem_Resume.Reset();
	m_sem_ChangingExecMode.Reset();

	m_ExecModeMutex.RecreateIfLocked();
	m_RunningLock.RecreateIfLocked();
	m_sem_event.Reset();
	m_ExecMode = ExecMode_Closed;
//	StateCheckInThread();
}

// Suspends emulation and closes the emulation state (including plugins) at the next PS2 vsync,
// and returns control to the calling thread; or does nothing if the core is already suspended.
//
// Parameters:
//   isNonblocking - if set to true then the function will not block for emulation suspension.
//      Defaults to false if parameter is not specified.  Performing non-blocking suspension
//      is mostly useful for starting certain non-Emu related gui activities (improves gui
//      responsiveness).
//
// Returns:
//   The previous suspension state; true if the thread was running or false if it was
//   suspended.
//
// Exceptions:
//   CancelEvent  - thrown if the thread is already in a Paused or Closing state.  Because
//      actions that pause emulation typically rely on plugins remaining loaded/active,
//      Suspension must cancel itself forcefully or risk crashing whatever other action is
//      in progress.
//
void SysFakeThread::Suspend( bool isBlocking )
{
	if (!pxAssertDev(!IsSelf(),"Suspend/Resume are not allowed from this thread.")) return;
	if (!IsRunning()) return;

	// shortcut ExecMode check to avoid deadlocking on redundant calls to Suspend issued
	// from Resume or OnResumeReady code.
	if( m_ExecMode == ExecMode_Closed ) return;

	{
		ScopedLock locker( m_ExecModeMutex );

		switch( m_ExecMode.load() )
		{
			// Invalid thread state, nothing to suspend
			case ExecMode_NoThreadYet:
			// Check again -- status could have changed since above.
			case ExecMode_Closed: return;

			case ExecMode_Pausing:
			case ExecMode_Paused:
				if( !isBlocking )
					throw Exception::CancelEvent( L"Cannot suspend in non-blocking fashion: Another thread is pausing the VM state." );
	
				m_ExecMode = ExecMode_Closing;
				m_sem_Resume.Post();
				m_sem_ChangingExecMode.Wait();
			break;
	
			case ExecMode_Opened:
				m_ExecMode = ExecMode_Closing;
			break;

			case ExecMode_Closing:
			break;
		}

		pxAssertDev( m_ExecMode == ExecMode_Closing, "ExecMode should be nothing other than Closing..." );
		m_sem_event.Post();
	}

	if( isBlocking )
		m_RunningLock.Wait();
}

// Returns:
//   The previous suspension state; true if the thread was running or false if it was
//   closed, not running, or paused.
//
void SysFakeThread::Pause()
{
	if( IsSelf() || !IsRunning() ) return;

	// shortcut ExecMode check to avoid deadlocking on redundant calls to Suspend issued
	// from Resume or OnResumeReady code.
	if( (m_ExecMode == ExecMode_Closed) || (m_ExecMode == ExecMode_Paused) ) return;

	{
		ScopedLock locker( m_ExecModeMutex );

		// Check again -- status could have changed since above.
		if( (m_ExecMode == ExecMode_Closed) || (m_ExecMode == ExecMode_Paused) ) return;

		if( m_ExecMode == ExecMode_Opened )
			m_ExecMode = ExecMode_Pausing;

		pxAssertDev( m_ExecMode == ExecMode_Pausing, "ExecMode should be nothing other than Pausing..." );

		OnPause();
		m_sem_event.Post();
	}

	m_RunningLock.Wait();
}

// Resumes the core execution state, or does nothing is the core is already running.  If
// settings were changed, resets will be performed as needed and emulation state resumed from
// memory savestates.
//
// Note that this is considered a non-blocking action.  Most times the state is safely resumed
// on return, but in the case of re-entrant or nested message handling the function may return
// before the thread has resumed.  If you need explicit behavior tied to the completion of the
// Resume, you'll need to bind callbacks to either OnResumeReady or OnResumeInThread.
//
// Exceptions:
//   PluginInitError     - thrown if a plugin fails init (init is performed on the current thread
//                         on the first time the thread is resumed from it's initial idle state)
//   ThreadCreationError - Insufficient system resources to create thread.
//
void SysFakeThread::Resume()
{
	if( IsSelf() ) return;
	if( m_ExecMode == ExecMode_Opened ) return;

	ScopedLock locker( m_ExecModeMutex );

	// Implementation Note:
	// The entire state coming out of a Wait is indeterminate because of user input
	// and pending messages being handled.  So after each call we do some seemingly redundant
	// sanity checks against m_ExecMode/m_Running status, and if something doesn't feel
	// right, we should abort; the user may have canceled the action before it even finished.

	switch( m_ExecMode.load() )
	{
		case ExecMode_Opened: return;

		case ExecMode_NoThreadYet:
		{
			Start();
			if( m_ExecMode == ExecMode_Opened ) return;
		}
		// fall through...

		case ExecMode_Closing:
		case ExecMode_Pausing:
			// we need to make sure and wait for the emuThread to enter a fully suspended
			// state before continuing...

			m_RunningLock.Wait();
			if( !m_running ) return;
			if( (m_ExecMode != ExecMode_Closed) && (m_ExecMode != ExecMode_Paused) ) return;
		break;

		case ExecMode_Paused:
		case ExecMode_Closed:
		break;
	}

	pxAssertDev( (m_ExecMode == ExecMode_Closed) || (m_ExecMode == ExecMode_Paused),
		"SysFakeThread is not in a closed/paused state?  wtf!" );

	OnResumeReady();
	m_ExecMode = ExecMode_Opened;
	m_sem_Resume.Post();
}


// --------------------------------------------------------------------------------------
//  SysFakeThread *Worker* Implementations
//    (Called from the context of this thread only)
// --------------------------------------------------------------------------------------

void SysFakeThread::OnStartInThread()
{
	m_RunningLock.Acquire();
//	m_detached = false;
	m_running = true;
	m_ExecMode = ExecMode_Closing;
}

void SysFakeThread::OnCleanupInThread()
{
	m_ExecMode = ExecMode_NoThreadYet;
	m_RunningLock.Release();
}

void SysFakeThread::OnSuspendInThread() {}
void SysFakeThread::OnResumeInThread( bool isSuspended ) {}

// Tests for Pause and Suspend/Close requests.  If the thread is trying to be paused or
// closed, it will enter a wait/holding pattern here in this method until the managing
// thread releases it.  Use the return value to detect if changes to the thread's state
// may have been changed (based on the rule that other threads are not allowed to modify
// this thread's state without pausing or closing it first, to prevent race conditions).
//
// Return value:
//   TRUE if the thread was paused or closed; FALSE if the thread
//   continued execution unimpeded.
bool SysFakeThread::StateCheckInThread()
{
	switch( m_ExecMode.load() )
	{

		case ExecMode_Opened:
			return false;

		// -------------------------------------
		case ExecMode_Pausing:
		{
			OnPauseInThread();
			m_ExecMode = ExecMode_Paused;
			m_RunningLock.Release();
		}
		// fallthrough...

		case ExecMode_Paused:
			while( m_ExecMode == ExecMode_Paused )
				m_sem_Resume.WaitWithoutYield();
		
			m_RunningLock.Acquire();
			if( m_ExecMode != ExecMode_Closing )
			{
				OnResumeInThread( false );
				break;
			}
			m_sem_ChangingExecMode.Post();
			
		// fallthrough if we're switching to closing state...

		// -------------------------------------
		case ExecMode_Closing:
		{
			OnSuspendInThread();
			m_ExecMode = ExecMode_Closed;
			m_RunningLock.Release();
		}
		// Fall through

		case ExecMode_Closed:
			while( m_ExecMode == ExecMode_Closed )
				m_sem_Resume.WaitWithoutYield();

			m_RunningLock.Acquire();
			OnResumeInThread( true );
		break;

		jNO_DEFAULT;
	}
	
	return true;
}
