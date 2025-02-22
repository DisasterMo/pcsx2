/////////////////////////////////////////////////////////////////////////////
// Name:        msvc/wx/setup.h
// Purpose:     wrapper around the real wx/setup.h for Visual C++
// Author:      Vadim Zeitlin
// Modified by:
// Created:     2004-12-12
// Copyright:   (c) 2004 Vadim Zeitlin <vadim@wxwindows.org>
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _MSC_VER
    #error "This file should only be included when using Microsoft Visual C++"
#endif

// VC++ IDE predefines _DEBUG and _UNICODE for the new projects itself, 
// the other symbols should be defined
// explicitly!

#include "wx/version.h"
#include "wx/cpp.h"

// notice that wxSUFFIX_DEBUG is a string but wxSUFFIX itself must be an
// identifier as string concatenation is not done inside #include where we
// need it
#ifdef _DEBUG
    #define wxSUFFIX_DEBUG "d"
    #ifdef _UNICODE
        #define wxSUFFIX ud
    #else // !_UNICODE
        #define wxSUFFIX d
    #endif // _UNICODE/!_UNICODE
#else
    #define wxSUFFIX_DEBUG ""
    #ifdef _UNICODE
        #define wxSUFFIX u
    #else // !_UNICODE
        // don't define wxSUFFIX at all as preprocessor operations don't work
        // with empty values so we need to check for this case specially below
    #endif // _UNICODE/!_UNICODE
#endif

// compiler-specific prefix: by default it's always just "vc" for compatibility
// reasons but if you use multiple MSVC versions you probably build them with
// COMPILER_PREFIX=vcXX and in this case you may want to either predefine
// wxMSVC_VERSION as "XX" or define wxMSVC_VERSION_AUTO to use the appropriate
// version depending on the compiler used
#ifdef wxMSVC_VERSION
    #define wxCOMPILER_PREFIX wxCONCAT(vc, wxMSVC_VERSION)
#elif defined(wxMSVC_VERSION_AUTO)
    #if _MSC_VER == 1200
        #define wxCOMPILER_PREFIX vc60
    #elif _MSC_VER == 1300
        #define wxCOMPILER_PREFIX vc70
    #elif _MSC_VER == 1310
        #define wxCOMPILER_PREFIX vc71
    #elif _MSC_VER == 1400
        #define wxCOMPILER_PREFIX vc80
    #elif _MSC_VER == 1500
        #define wxCOMPILER_PREFIX vc90
    #elif _MSC_VER == 1600
        #define wxCOMPILER_PREFIX vc100
    #elif _MSC_VER == 1700
        #define wxCOMPILER_PREFIX vc110
    #elif _MSC_VER == 1800
        #define wxCOMPILER_PREFIX vc120
    #elif _MSC_VER == 1900
        #define wxCOMPILER_PREFIX vc140
    #else
        #error "Unknown MSVC compiler version, please report to wx-dev."
    #endif
#else
    #define wxCOMPILER_PREFIX vc
#endif

// architecture-specific part: not used (again, for compatibility), for x86
#if defined(_M_X64)
    #define wxARCH_SUFFIX _x64
#elif defined(_M_IA64)
    #define wxARCH_SUFFIX _ia64
#else // assume _M_IX86
    #define wxARCH_SUFFIX
#endif

// Ensure the library configuration is defined
#ifndef wxCFG
    #define wxCFG
#endif

// Construct the path for the subdirectory under /lib/ that the included setup.h
// will be used from
#define wxLIB_SUBDIR \
	wxCONCAT4(wxCOMPILER_PREFIX, wxARCH_SUFFIX, _lib, wxCFG)

// The user can predefine a different prefix if not using the default MSW port
// with MSVC.
#ifndef wxTOOLKIT_PREFIX
    #if defined(__WXGTK__)
        #define wxTOOLKIT_PREFIX gtk2
    #else
        #define wxTOOLKIT_PREFIX msw
    #endif
#endif // wxTOOLKIT_PREFIX

// the real setup.h header file we need is in the build-specific directory,
// construct the path to it
#ifdef wxSUFFIX
    #define wxSETUPH_PATH \
        wxCONCAT6(../../../lib/, wxLIB_SUBDIR, /, wxTOOLKIT_PREFIX, wxSUFFIX, /wx/setup.h)
#else // suffix is empty
    #define wxSETUPH_PATH \
        wxCONCAT5(../../../lib/, wxLIB_SUBDIR, /, wxTOOLKIT_PREFIX, /wx/setup.h)
#endif

#define wxSETUPH_PATH_STR wxSTRINGIZE(wxSETUPH_PATH)

#include wxSETUPH_PATH_STR


// the library names depend on the build, these macro builds the correct
// library name for the given base name
#ifdef wxSUFFIX
    #define wxSUFFIX_STR wxSTRINGIZE(wxSUFFIX)
#else // suffix is empty
    #define wxSUFFIX_STR ""
#endif
#define wxSHORT_VERSION_STRING \
    wxSTRINGIZE(wxMAJOR_VERSION) wxSTRINGIZE(wxMINOR_VERSION)

#define wxWX_LIB_NAME(name, subname) \
    "wx" name wxSHORT_VERSION_STRING wxSUFFIX_STR subname

#define wxBASE_LIB_NAME(name) wxWX_LIB_NAME("base", "_" name)
#define wxTOOLKIT_LIB_NAME(name) wxWX_LIB_NAME(wxSTRINGIZE(wxTOOLKIT_PREFIX), "_" name)

// This one is for 3rd party libraries: they don't have the version number
// in their names and usually exist in ANSI version only (except for regex)
//
// 3rd party libraries are also are not linked in when using DLLs as they're
// embedded inside our own DLLs and don't need to be linked with the user code.
#define wx3RD_PARTY_LIB_NAME(name) "wx" name wxSUFFIX_DEBUG

// special version for regex as it does have a Unicode version
#define wx3RD_PARTY_LIB_NAME_U(name) "wx" name wxSUFFIX_STR

#pragma comment(lib, wxWX_LIB_NAME("base", ""))

#ifndef wxNO_NET_LIB
#pragma comment(lib, "wsock32")
    #pragma comment(lib, wxBASE_LIB_NAME("net"))
#endif

// Make sure all required system libraries are added to the linker too when
// using static libraries.
#pragma comment(lib, "kernel32")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "winspool")
#pragma comment(lib, "winmm")
#pragma comment(lib, "shell32")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "oleaut32")
#pragma comment(lib, "uuid")
#pragma comment(lib, "rpcrt4")
#pragma comment(lib, "advapi32")

#ifdef __WXGTK__
#pragma comment(lib, "gtk-win32-2.0.lib")
#pragma comment(lib, "gdk-win32-2.0.lib")
#pragma comment(lib, "pangocairo-1.0.lib")
#pragma comment(lib, "gdk_pixbuf-2.0.lib")
#pragma comment(lib, "cairo.lib")
#pragma comment(lib, "pango-1.0.lib")
#pragma comment(lib, "gobject-2.0.lib")
#pragma comment(lib, "gthread-2.0.lib")
#pragma comment(lib, "glib-2.0.lib")
#endif
