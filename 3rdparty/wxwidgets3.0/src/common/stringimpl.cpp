/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/stringimpl.cpp
// Purpose:     wxString class
// Author:      Vadim Zeitlin, Ryan Norton
// Modified by:
// Created:     29/01/98
// Copyright:   (c) 1998 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
//              (c) 2004 Ryan Norton <wxprojects@comcast.net>
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

/*
 * About ref counting:
 *  1) all empty strings use g_strEmpty, nRefs = -1 (set in Init())
 *  2) AllocBuffer() sets nRefs to 1, Lock() increments it by one
 *  3) Unlock() decrements nRefs and frees memory if it goes to 0
 */

// ===========================================================================
// headers, declarations, constants
// ===========================================================================

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include "wx/stringimpl.h"
    #include "wx/wxcrt.h"
#endif

#include <ctype.h>

#ifndef __WXWINCE__
    #include <errno.h>
#endif

#include <string.h>
#include <stdlib.h>

// allocating extra space for each string consumes more memory but speeds up
// the concatenation operations (nLen is the current string's length)
// NB: EXTRA_ALLOC must be >= 0!
#define EXTRA_ALLOC       (19 - nLen % 16)


// string handling functions used by wxString:
#if wxUSE_UNICODE_UTF8
    #define wxStringMemcpy   memcpy
    #define wxStringMemcmp   memcmp
    #define wxStringMemchr   memchr
#else
    #define wxStringMemcpy   wxTmemcpy
    #define wxStringMemcmp   wxTmemcmp
    #define wxStringMemchr   wxTmemchr
#endif


// ---------------------------------------------------------------------------
// static class variables definition
// ---------------------------------------------------------------------------

#if !wxUSE_STL_BASED_WXSTRING
//According to STL _must_ be a -1 size_t
const size_t wxStringImpl::npos = (size_t) -1;
#endif

// ----------------------------------------------------------------------------
// static data
// ----------------------------------------------------------------------------

#if wxUSE_STL_BASED_WXSTRING

// FIXME-UTF8: get rid of this, have only one wxEmptyString
#if wxUSE_UNICODE_UTF8
const wxStringCharType WXDLLIMPEXP_BASE *wxEmptyStringImpl = "";
#endif
const wxChar WXDLLIMPEXP_BASE *wxEmptyString = wxT("");

#else

// for an empty string, GetStringData() will return this address: this
// structure has the same layout as wxStringData and it's data() method will
// return the empty string (dummy pointer)
static const struct
{
  wxStringData data;
  wxStringCharType dummy;
} g_strEmpty = { {-1, 0, 0}, wxT('\0') };

// empty C style string: points to 'string data' byte of g_strEmpty
#if wxUSE_UNICODE_UTF8
// FIXME-UTF8: get rid of this, have only one wxEmptyString
const wxStringCharType WXDLLIMPEXP_BASE *wxEmptyStringImpl = &g_strEmpty.dummy;
const wxChar WXDLLIMPEXP_BASE *wxEmptyString = wxT("");
#else
const wxStringCharType WXDLLIMPEXP_BASE *wxEmptyString = &g_strEmpty.dummy;
#endif

#endif


#if !wxUSE_STL_BASED_WXSTRING
// ===========================================================================
// wxStringData class deallocation
// ===========================================================================

#if defined(__VISUALC__) && defined(_MT) && !defined(_DLL)
#  pragma message (__FILE__ ": building with Multithreaded non DLL runtime has a performance impact on wxString!")
void wxStringData::Free()
{
    free(this);
}
#endif

// ===========================================================================
// wxStringImpl
// ===========================================================================

// takes nLength elements of psz starting at nPos
void wxStringImpl::InitWith(const wxStringCharType *psz,
                            size_t nPos, size_t nLength)
{
  Init();

  // if the length is not given, assume the string to be NUL terminated
  if ( nLength == npos )
    nLength = wxStrlen(psz + nPos);

  if ( nLength > 0 ) {
    // trailing '\0' is written in AllocBuffer()
    if ( !AllocBuffer(nLength) )
      return;
    wxStringMemcpy(m_pchData, psz + nPos, nLength);
  }
}

wxStringImpl::wxStringImpl(const_iterator first, const_iterator last)
{
  if ( last >= first )
    InitWith(first.GetPtr(), 0, last - first);
  else
    Init();
}

wxStringImpl::wxStringImpl(size_type n, wxStringCharType ch)
{
  Init();
  append(n, ch);
}

// ---------------------------------------------------------------------------
// memory allocation
// ---------------------------------------------------------------------------

// allocates memory needed to store a C string of length nLen
bool wxStringImpl::AllocBuffer(size_t nLen)
{
  // allocate memory:
  // 1) one extra character for '\0' termination
  // 2) sizeof(wxStringData) for housekeeping info
  wxStringData* pData = (wxStringData*)
    malloc(sizeof(wxStringData) + (nLen + EXTRA_ALLOC + 1)*sizeof(wxStringCharType));

  if ( pData == NULL ) {
    // allocation failures are handled by the caller
    return false;
  }

  pData->nRefs        = 1;
  pData->nDataLength  = nLen;
  pData->nAllocLength = nLen + EXTRA_ALLOC;
  m_pchData           = pData->data();  // data starts after wxStringData
  m_pchData[nLen]     = wxT('\0');
  return true;
}

// must be called before changing this string
bool wxStringImpl::CopyBeforeWrite()
{
  wxStringData* pData = GetStringData();

  if ( pData->IsShared() ) {
    pData->Unlock();                // memory not freed because shared
    size_t nLen = pData->nDataLength;
    if ( !AllocBuffer(nLen) ) {
      // allocation failures are handled by the caller
      return false;
    }
    wxStringMemcpy(m_pchData, pData->data(), nLen);
  }

  return true;
}

// must be called before replacing contents of this string
bool wxStringImpl::AllocBeforeWrite(size_t nLen)
{
  // must not share string and must have enough space
  wxStringData* pData = GetStringData();
  if ( pData->IsShared() || pData->IsEmpty() ) {
    // can't work with old buffer, get new one
    pData->Unlock();
    if ( !AllocBuffer(nLen) ) {
      // allocation failures are handled by the caller
      return false;
    }
  }
  else {
    if ( nLen > pData->nAllocLength ) {
      // realloc the buffer instead of calling malloc() again, this is more
      // efficient
      nLen += EXTRA_ALLOC;

      pData = (wxStringData*)
          realloc(pData,
                  sizeof(wxStringData) + (nLen + 1)*sizeof(wxStringCharType));

      if ( pData == NULL ) {
        // allocation failures are handled by the caller
        // keep previous data since reallocation failed
        return false;
      }

      pData->nAllocLength = nLen;
      m_pchData = pData->data();
    }
  }

  // it doesn't really matter what the string length is as it's going to be
  // overwritten later but, for extra safety, set it to 0 for now as we may
  // have some junk in m_pchData
  GetStringData()->nDataLength = 0;

  return true;
}

wxStringImpl& wxStringImpl::append(size_t n, wxStringCharType ch)
{
    size_type len = length();

    if ( !Alloc(len + n) || !CopyBeforeWrite() )
      return *this;
    GetStringData()->nDataLength = len + n;
    m_pchData[len + n] = '\0';
    for ( size_t i = 0; i < n; ++i )
        m_pchData[len + i] = ch;
    return *this;
}

void wxStringImpl::resize(size_t nSize, wxStringCharType ch)
{
    size_t len = length();

    if ( nSize < len )
    {
        erase(begin() + nSize, end());
    }
    else if ( nSize > len )
    {
        append(nSize - len, ch);
    }
    //else: we have exactly the specified length, nothing to do
}

// allocate enough memory for nLen characters
bool wxStringImpl::Alloc(size_t nLen)
{
  wxStringData *pData = GetStringData();
  if ( pData->nAllocLength <= nLen ) {
    if ( pData->IsEmpty() ) {
      nLen += EXTRA_ALLOC;

      pData = (wxStringData *)
             malloc(sizeof(wxStringData) + (nLen + 1)*sizeof(wxStringCharType));

      if ( pData == NULL ) {
        // allocation failure handled by caller
        return false;
      }

      pData->nRefs = 1;
      pData->nDataLength = 0;
      pData->nAllocLength = nLen;
      m_pchData = pData->data();  // data starts after wxStringData
      m_pchData[0u] = wxT('\0');
    }
    else if ( pData->IsShared() ) {
      pData->Unlock();                // memory not freed because shared
      size_t nOldLen = pData->nDataLength;
      if ( !AllocBuffer(nLen) ) {
        // allocation failure handled by caller
        return false;
      }
      // +1 to copy the terminator, too
      memcpy(m_pchData, pData->data(), (nOldLen+1)*sizeof(wxStringCharType));
      GetStringData()->nDataLength = nOldLen;
    }
    else {
      nLen += EXTRA_ALLOC;

      pData = (wxStringData *)
        realloc(pData, sizeof(wxStringData) + (nLen + 1)*sizeof(wxStringCharType));

      if ( pData == NULL ) {
        // allocation failure handled by caller
        // keep previous data since reallocation failed
        return false;
      }

      // it's not important if the pointer changed or not (the check for this
      // is not faster than assigning to m_pchData in all cases)
      pData->nAllocLength = nLen;
      m_pchData = pData->data();
    }
  }
  //else: we've already got enough
  return true;
}

wxStringImpl::iterator wxStringImpl::begin()
{
    if ( !empty() )
        CopyBeforeWrite();
    return m_pchData;
}

wxStringImpl::iterator wxStringImpl::end()
{
    if ( !empty() )
        CopyBeforeWrite();
    return m_pchData + length();
}

wxStringImpl::iterator wxStringImpl::erase(iterator it)
{
    size_type idx = it - begin();
    erase(idx, 1);
    return begin() + idx;
}

wxStringImpl& wxStringImpl::erase(size_t nStart, size_t nLen)
{
    size_t strLen = length() - nStart;
    // delete nLen or up to the end of the string characters
    nLen = strLen < nLen ? strLen : nLen;
    wxStringImpl strTmp(c_str(), nStart);
    strTmp.append(c_str() + nStart + nLen, length() - nStart - nLen);

    swap(strTmp);
    return *this;
}

wxStringImpl& wxStringImpl::insert(size_t nPos,
                                   const wxStringCharType *sz, size_t n)
{
    if ( n == npos ) n = wxStrlen(sz);
    if ( n == 0 ) return *this;

    if ( !Alloc(length() + n) || !CopyBeforeWrite() )
        return *this;

    memmove(m_pchData + nPos + n, m_pchData + nPos,
            (length() - nPos) * sizeof(wxStringCharType));
    memcpy(m_pchData + nPos, sz, n * sizeof(wxStringCharType));
    GetStringData()->nDataLength = length() + n;
    m_pchData[length()] = '\0';

    return *this;
}

void wxStringImpl::swap(wxStringImpl& str)
{
    wxStringCharType* tmp = str.m_pchData;
    str.m_pchData = m_pchData;
    m_pchData = tmp;
}

size_t wxStringImpl::find(const wxStringImpl& str, size_t nStart) const
{
    // deal with the special case of empty string first
    const size_t nLen = length();
    const size_t nLenOther = str.length();

    if ( !nLenOther )
    {
        // empty string is a substring of anything
        return 0;
    }

    if ( !nLen )
    {
        // the other string is non empty so can't be our substring
        return npos;
    }

    const wxStringCharType * const other = str.c_str();

    // anchor
    const wxStringCharType* p =
        (const wxStringCharType*)wxStringMemchr(c_str() + nStart,
                                                *other,
                                                nLen - nStart);

    if ( !p )
        return npos;

    while ( p - c_str() + nLenOther <= nLen &&
            wxStringMemcmp(p, other, nLenOther) )
    {
        p++;

        // anchor again
        p = (const wxStringCharType*)
                wxStringMemchr(p, *other, nLen - (p - c_str()));

        if ( !p )
            return npos;
    }

    return p - c_str() + nLenOther <= nLen ? p - c_str() : npos;
}

size_t wxStringImpl::find(const wxStringCharType* sz,
                          size_t nStart, size_t n) const
{
    return find(wxStringImpl(sz, n), nStart);
}

size_t wxStringImpl::find(wxStringCharType ch, size_t nStart) const
{
    const wxStringCharType *p = (const wxStringCharType*)
        wxStringMemchr(c_str() + nStart, ch, length() - nStart);

    return p == NULL ? npos : p - c_str();
}

size_t wxStringImpl::rfind(const wxStringImpl& str, size_t nStart) const
{
    if ( length() >= str.length() )
    {
        // avoids a corner case later
        if ( empty() && str.empty() )
            return 0;

        // "top" is the point where search starts from
        size_t top = length() - str.length();

        if ( nStart == npos )
            nStart = length() - 1;
        if ( nStart < top )
            top = nStart;

        const wxStringCharType *cursor = c_str() + top;
        do
        {
            if ( wxStringMemcmp(cursor, str.c_str(), str.length()) == 0 )
            {
                return cursor - c_str();
            }
        } while ( cursor-- > c_str() );
    }

    return npos;
}

size_t wxStringImpl::rfind(const wxStringCharType* sz,
                           size_t nStart, size_t n) const
{
    return rfind(wxStringImpl(sz, n), nStart);
}

size_t wxStringImpl::rfind(wxStringCharType ch, size_t nStart) const
{
    if ( nStart == npos )
        nStart = length();

    const wxStringCharType *actual;
    for ( actual = c_str() + ( nStart == npos ? length() : nStart + 1 );
          actual > c_str(); --actual )
    {
        if ( *(actual - 1) == ch )
            return (actual - 1) - c_str();
    }

    return npos;
}

wxStringImpl& wxStringImpl::replace(size_t nStart, size_t nLen,
                                    const wxStringCharType *sz, size_t nCount)
{
    // check and adjust parameters
    const size_t lenOld = length();
    size_t nEnd = nStart + nLen;
    if ( nLen > lenOld - nStart )
    {
        // nLen may be out of range, as it can be npos, just clump it down
        nLen = lenOld - nStart;
        nEnd = lenOld;
    }

    if ( nCount == npos )
        nCount = wxStrlen(sz);

    // build the new string from 3 pieces: part of this string before nStart,
    // the new substring and the part of this string after nStart+nLen
    wxStringImpl tmp;
    const size_t lenNew = lenOld + nCount - nLen;
    if ( lenNew )
    {
        tmp.AllocBuffer(lenOld + nCount - nLen);

        wxStringCharType *dst = tmp.m_pchData;
        memcpy(dst, m_pchData, nStart*sizeof(wxStringCharType));
        dst += nStart;

        memcpy(dst, sz, nCount*sizeof(wxStringCharType));
        dst += nCount;

        memcpy(dst, m_pchData + nEnd, (lenOld - nEnd)*sizeof(wxStringCharType));
    }

    // and replace this string contents with the new one
    swap(tmp);
    return *this;
}

wxStringImpl wxStringImpl::substr(size_t nStart, size_t nLen) const
{
  if ( nLen == npos )
    nLen = length() - nStart;
  return wxStringImpl(*this, nStart, nLen);
}

// assigns one string to another
wxStringImpl& wxStringImpl::operator=(const wxStringImpl& stringSrc)
{
  // don't copy string over itself
  if ( m_pchData != stringSrc.m_pchData ) {
    if ( stringSrc.GetStringData()->IsEmpty() ) {
      Reinit();
    }
    else {
      // adjust references
      GetStringData()->Unlock();
      m_pchData = stringSrc.m_pchData;
      GetStringData()->Lock();
    }
  }

  return *this;
}

// assigns a single character
wxStringImpl& wxStringImpl::operator=(wxStringCharType ch)
{
  wxStringCharType c(ch);
  AssignCopy(1, &c);
  return *this;
}

// assigns C string
wxStringImpl& wxStringImpl::operator=(const wxStringCharType *psz)
{
  AssignCopy(wxStrlen(psz), psz);
  return *this;
}

// helper function: does real copy
bool wxStringImpl::AssignCopy(size_t nSrcLen,
                              const wxStringCharType *pszSrcData)
{
  if ( nSrcLen == 0 ) {
    Reinit();
  }
  else {
    if ( !AllocBeforeWrite(nSrcLen) ) {
      // allocation failure handled by caller
      return false;
    }

    // use memmove() and not memcpy() here as we might be copying from our own
    // buffer in case of assignment such as "s = s.c_str()" (see #11294)
    memmove(m_pchData, pszSrcData, nSrcLen*sizeof(wxStringCharType));

    GetStringData()->nDataLength = nSrcLen;
    m_pchData[nSrcLen] = wxT('\0');
  }
  return true;
}

// ---------------------------------------------------------------------------
// string concatenation
// ---------------------------------------------------------------------------

// add something to this string
bool wxStringImpl::ConcatSelf(size_t nSrcLen,
                              const wxStringCharType *pszSrcData,
                              size_t nMaxLen)
{
  nSrcLen = nSrcLen < nMaxLen ? nSrcLen : nMaxLen;

  // concatenating an empty string is a NOP
  if ( nSrcLen > 0 ) {
    wxStringData *pData = GetStringData();
    size_t nLen = pData->nDataLength;

    // take special care when appending part of this string to itself: the code
    // below reallocates our buffer and this invalidates pszSrcData pointer so
    // we have to copy it in another temporary string in this case (but avoid
    // doing this unnecessarily)
    if ( pszSrcData >= m_pchData && pszSrcData < m_pchData + nLen )
    {
        wxStringImpl tmp(pszSrcData, nSrcLen);
        return ConcatSelf(nSrcLen, tmp.m_pchData, nSrcLen);
    }

    size_t nNewLen = nLen + nSrcLen;

    // alloc new buffer if current is too small
    if ( pData->IsShared() ) {
      // we have to allocate another buffer
      wxStringData* pOldData = GetStringData();
      if ( !AllocBuffer(nNewLen) ) {
          // allocation failure handled by caller
          return false;
      }
      memcpy(m_pchData, pOldData->data(), nLen*sizeof(wxStringCharType));
      pOldData->Unlock();
    }
    else if ( nNewLen > pData->nAllocLength ) {
      reserve(nNewLen);
      // we have to grow the buffer
      if ( capacity() < nNewLen ) {
          // allocation failure handled by caller
          return false;
      }
    }

    // fast concatenation - all is done in our buffer
    memcpy(m_pchData + nLen, pszSrcData, nSrcLen*sizeof(wxStringCharType));

    m_pchData[nNewLen] = wxT('\0');          // put terminating '\0'
    GetStringData()->nDataLength = nNewLen; // and fix the length
  }
  //else: the string to append was empty
  return true;
}

// get the pointer to writable buffer of (at least) nLen bytes
wxStringCharType *wxStringImpl::DoGetWriteBuf(size_t nLen)
{
  // allocation failure handled by caller
  if ( !AllocBeforeWrite(nLen) )
    return NULL;

  GetStringData()->Validate(false);

  return m_pchData;
}

// put string back in a reasonable state after GetWriteBuf
void wxStringImpl::DoUngetWriteBuf()
{
  DoUngetWriteBuf(wxStrlen(m_pchData));
}

void wxStringImpl::DoUngetWriteBuf(size_t nLen)
{
  wxStringData * const pData = GetStringData();

  // the strings we store are always NUL-terminated
  pData->data()[nLen] = wxT('\0');
  pData->nDataLength = nLen;
  pData->Validate(true);
}

#endif // !wxUSE_STL_BASED_WXSTRING
