/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/module.cpp
// Purpose:     Modules initialization/destruction
// Author:      Wolfram Gloger/adapted by Guilhem Lavaux
// Modified by:
// Created:     04/11/98
// Copyright:   (c) Wolfram Gloger and Guilhem Lavaux
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#include "wx/module.h"

#ifndef WX_PRECOMP
    #include "wx/hash.h"
#endif

#include "wx/listimpl.cpp"

#define TRACE_MODULE wxT("module")

WX_DEFINE_LIST(wxModuleList)

wxIMPLEMENT_ABSTRACT_CLASS(wxModule, wxObject)

wxModuleList wxModule::m_modules;

void wxModule::RegisterModule(wxModule* module)
{
    module->m_state = State_Registered;
    m_modules.Append(module);
}

void wxModule::UnregisterModule(wxModule* module)
{
    m_modules.DeleteObject(module);
    delete module;
}

// Collect up all module-derived classes, create an instance of each,
// and register them.
void wxModule::RegisterModules()
{
    for (wxClassInfo::const_iterator it  = wxClassInfo::begin_classinfo(),
                                     end = wxClassInfo::end_classinfo();
         it != end; ++it)
    {
        const wxClassInfo* classInfo = *it;

        if ( classInfo->IsKindOf(wxCLASSINFO(wxModule)) &&
             (classInfo != (& (wxModule::ms_classInfo))) )
        {
            wxModule* module = (wxModule *)classInfo->CreateObject();
            wxModule::RegisterModule(module);
        }
    }
}

bool wxModule::DoInitializeModule(wxModule *module,
                                  wxModuleList &initializedModules)
{
    if ( module->m_state == State_Initializing )
        return false;

    module->m_state = State_Initializing;

    const wxArrayClassInfo& dependencies = module->m_dependencies;

    // satisfy module dependencies by loading them before the current module
    for ( unsigned int i = 0; i < dependencies.size(); ++i )
    {
        wxClassInfo * cinfo = dependencies[i];

        // Check if the module is already initialized
        wxModuleList::compatibility_iterator node;
        for ( node = initializedModules.GetFirst(); node; node = node->GetNext() )
        {
            if ( node->GetData()->GetClassInfo() == cinfo )
                break;
        }

        if ( node )
        {
            // this dependency is already initialized, nothing to do
            continue;
        }

        // find the module in the registered modules list
        for ( node = m_modules.GetFirst(); node; node = node->GetNext() )
        {
            wxModule *moduleDep = node->GetData();
            if ( moduleDep->GetClassInfo() == cinfo )
            {
                if ( !DoInitializeModule(moduleDep, initializedModules ) )
                {
                    // failed to initialize a dependency, so fail this one too
                    return false;
                }

                break;
            }
        }

        if ( !node )
            return false;
    }

    if ( !module->Init() )
        return false;

    module->m_state = State_Initialized;
    initializedModules.Append(module);

    return true;
}

// Initialize user-defined modules
bool wxModule::InitializeModules()
{
    wxModuleList initializedModules;

    for ( wxModuleList::compatibility_iterator node = m_modules.GetFirst();
          node;
          node = node->GetNext() )
    {
        wxModule *module = node->GetData();

        // the module could have been already initialized as dependency of
        // another one
        if ( module->m_state == State_Registered )
        {
            if ( !DoInitializeModule( module, initializedModules ) )
            {
                // failed to initialize all modules, so clean up the already
                // initialized ones
                DoCleanUpModules(initializedModules);

                return false;
            }
        }
    }

    // remember the real initialisation order
    m_modules = initializedModules;

    return true;
}

// Clean up all currently initialized modules
void wxModule::DoCleanUpModules(const wxModuleList& modules)
{
    // cleanup user-defined modules in the reverse order compared to their
    // initialization -- this ensures that dependencies are respected
    for ( wxModuleList::compatibility_iterator node = modules.GetLast();
          node;
          node = node->GetPrevious() )
    {
        wxModule * module = node->GetData();
        module->Exit();
        module->m_state = State_Registered;
    }

    // clear all modules, even the non-initialized ones
    WX_CLEAR_LIST(wxModuleList, m_modules);
}
