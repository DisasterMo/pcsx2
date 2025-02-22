/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/stream.cpp
// Purpose:     wxStream base classes
// Author:      Guilhem Lavaux
// Modified by: VZ (23.11.00) to fix realloc()ing new[]ed memory,
//                            general code review
// Created:     11/07/98
// Copyright:   (c) Guilhem Lavaux
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#if wxUSE_STREAMS

#include "wx/stream.h"

#include <ctype.h>
#include "wx/textfile.h"
#include "wx/scopeguard.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// the temporary buffer size used when copying from stream to stream
#define BUF_TEMP_SIZE 4096

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxStreamBuffer
// ----------------------------------------------------------------------------

void wxStreamBuffer::SetError(wxStreamError err)
{
   if ( m_stream && m_stream->m_lasterror == wxSTREAM_NO_ERROR )
       m_stream->m_lasterror = err;
}

void wxStreamBuffer::InitBuffer()
{
    m_buffer_start =
    m_buffer_end =
    m_buffer_pos = NULL;

    // if we are going to allocate the buffer, we should free it later as well
    m_destroybuf = true;
}

void wxStreamBuffer::Init()
{
    InitBuffer();

    m_fixed = true;
}

void wxStreamBuffer::InitWithStream(wxStreamBase& stream, BufMode mode)
{
    Init();

    m_stream = &stream;
    m_mode = mode;

    m_flushable = true;
}

wxStreamBuffer::wxStreamBuffer(BufMode mode)
{
    Init();

    m_stream = NULL;
    m_mode = mode;

    m_flushable = false;
}

wxStreamBuffer::wxStreamBuffer(const wxStreamBuffer& buffer)
{
    m_buffer_start = buffer.m_buffer_start;
    m_buffer_end = buffer.m_buffer_end;
    m_buffer_pos = buffer.m_buffer_pos;
    m_fixed = buffer.m_fixed;
    m_flushable = buffer.m_flushable;
    m_stream = buffer.m_stream;
    m_mode = buffer.m_mode;
    m_destroybuf = false;
}

void wxStreamBuffer::FreeBuffer()
{
    if ( m_destroybuf )
    {
        free(m_buffer_start);
        m_buffer_start = NULL;
    }
}

wxStreamBuffer::~wxStreamBuffer()
{
    FreeBuffer();
}

wxInputStream *wxStreamBuffer::GetInputStream() const
{
    return m_mode == write ? NULL : (wxInputStream *)m_stream;
}

wxOutputStream *wxStreamBuffer::GetOutputStream() const
{
    return m_mode == read ? NULL : (wxOutputStream *)m_stream;
}

void wxStreamBuffer::SetBufferIO(void *buffer_start,
                                 void *buffer_end,
                                 bool takeOwnership)
{
    SetBufferIO(buffer_start, (char *)buffer_end - (char *)buffer_start,
                takeOwnership);
}

void wxStreamBuffer::SetBufferIO(void *start,
                                 size_t len,
                                 bool takeOwnership)
{
    // start by freeing the old buffer
    FreeBuffer();

    m_buffer_start = (char *)start;
    m_buffer_end   = m_buffer_start + len;

    // if we own it, we free it
    m_destroybuf = takeOwnership;

    ResetBuffer();
}

void wxStreamBuffer::SetBufferIO(size_t bufsize)
{
    if ( bufsize )
    {
        // this will free the old buffer and allocate the new one
        SetBufferIO(malloc(bufsize), bufsize, true /* take ownership */);
    }
    else // no buffer size => no buffer
    {
        // still free the old one
        FreeBuffer();
        InitBuffer();
    }
}

void wxStreamBuffer::ResetBuffer()
{
    if ( m_stream )
    {
        m_stream->Reset();
        m_stream->m_lastcount = 0;
    }

    m_buffer_pos = m_mode == read && m_flushable
                        ? m_buffer_end
                        : m_buffer_start;
}

void wxStreamBuffer::Truncate()
{
    size_t new_size = m_buffer_pos - m_buffer_start;
    if ( m_buffer_pos == m_buffer_end )
        return;

    if ( !new_size )
    {
        FreeBuffer();
        InitBuffer();
        return;
    }

    char *new_start = (char *)realloc(m_buffer_start, new_size);
    m_buffer_start = new_start;
    m_buffer_end = m_buffer_start + new_size;
    m_buffer_pos = m_buffer_end;
}

// fill the buffer with as much data as possible (only for read buffers)
bool wxStreamBuffer::FillBuffer()
{
    wxInputStream *inStream = GetInputStream();

    // It's legal to have no stream, so we don't complain about it just return false
    if ( !inStream )
        return false;

    size_t count = inStream->OnSysRead(GetBufferStart(), GetBufferSize());
    if ( !count )
        return false;

    m_buffer_end = m_buffer_start + count;
    m_buffer_pos = m_buffer_start;

    return true;
}

// write the buffer contents to the stream (only for write buffers)
bool wxStreamBuffer::FlushBuffer()
{
    // FIXME: what is this check for? (VZ)
    if ( m_buffer_pos == m_buffer_start )
        return false;

    wxOutputStream *outStream = GetOutputStream();

    size_t current = m_buffer_pos - m_buffer_start;
    size_t count = outStream->OnSysWrite(m_buffer_start, current);
    if ( count != current )
        return false;

    m_buffer_pos = m_buffer_start;

    return true;
}

size_t wxStreamBuffer::GetDataLeft()
{
    /* Why is this done? RR. */
    if ( m_buffer_pos == m_buffer_end && m_flushable)
        FillBuffer();

    return GetBytesLeft();
}

// copy up to size bytes from our buffer into the provided one
void wxStreamBuffer::GetFromBuffer(void *buffer, size_t size)
{
    // don't get more bytes than left in the buffer
    size_t left = GetBytesLeft();

    if ( size > left )
        size = left;

    memcpy(buffer, m_buffer_pos, size);
    m_buffer_pos += size;
}

// copy the contents of the provided buffer into this one
void wxStreamBuffer::PutToBuffer(const void *buffer, size_t size)
{
    size_t left = GetBytesLeft();

    if ( size > left )
    {
        if ( m_fixed )
        {
            // we can't realloc the buffer, so just copy what we can
            size = left;
        }
        else // !m_fixed
        {
            // realloc the buffer to have enough space for the data
            if ( m_buffer_pos + size > m_buffer_end )
            {
                size_t delta = m_buffer_pos - m_buffer_start;
                size_t new_size = delta + size;

                char *startOld = m_buffer_start;
                m_buffer_start = (char *)realloc(m_buffer_start, new_size);
                if ( !m_buffer_start )
                {
                    // don't leak memory if realloc() failed
                    m_buffer_start = startOld;

                    // what else can we do?
                    return;
                }

                // adjust the pointers invalidated by realloc()
                m_buffer_pos = m_buffer_start + delta;
                m_buffer_end = m_buffer_start + new_size;
            } // else: the buffer is big enough
        }
    }

    memcpy(m_buffer_pos, buffer, size);
    m_buffer_pos += size;
}

void wxStreamBuffer::PutChar(char c)
{
    wxOutputStream *outStream = GetOutputStream();

    // if we don't have buffer at all, just forward this call to the stream,
    if ( !HasBuffer() )
    {
        outStream->OnSysWrite(&c, sizeof(c));
    }
    else
    {
        // otherwise check we have enough space left
        if ( !GetDataLeft() && !FlushBuffer() )
        {
            // we don't
            SetError(wxSTREAM_WRITE_ERROR);
        }
        else
        {
            PutToBuffer(&c, sizeof(c));
            m_stream->m_lastcount = 1;
        }
    }
}

char wxStreamBuffer::GetChar()
{
    wxInputStream *inStream = GetInputStream();

    char c;
    if ( !HasBuffer() )
        inStream->OnSysRead(&c, sizeof(c));
    else
    {
        if ( !GetDataLeft() )
        {
            SetError(wxSTREAM_READ_ERROR);
            c = 0;
        }
        else
        {
            GetFromBuffer(&c, sizeof(c));
            m_stream->m_lastcount = 1;
        }
    }

    return c;
}

size_t wxStreamBuffer::Read(void *buffer, size_t size)
{
    /* Clear buffer first */
    memset(buffer, 0x00, size);

    // lasterror is reset before all new IO calls
    if ( m_stream )
        m_stream->Reset();

    size_t readBytes;
    if ( !HasBuffer() )
    {
        wxInputStream *inStream = GetInputStream();
        readBytes = inStream->OnSysRead(buffer, size);
    }
    else // we have a buffer, use it
    {
        size_t orig_size = size;

        while ( size > 0 )
        {
            size_t left = GetDataLeft();

            // if the requested number of bytes if greater than the buffer
            // size, read data in chunks
            if ( size > left )
            {
                GetFromBuffer(buffer, left);
                size -= left;
                buffer = (char *)buffer + left;

                if ( !FillBuffer() )
                {
                    SetError(wxSTREAM_EOF);
                    break;
                }
            }
            else // otherwise just do it in one gulp
            {
                GetFromBuffer(buffer, size);
                size = 0;
            }
        }

        readBytes = orig_size - size;
    }

    if ( m_stream )
        m_stream->m_lastcount = readBytes;

    return readBytes;
}

// this should really be called "Copy()"
size_t wxStreamBuffer::Read(wxStreamBuffer *dbuf)
{
    char buf[BUF_TEMP_SIZE];
    size_t nRead,
           total = 0;

    do
    {
        nRead = Read(buf, WXSIZEOF(buf));
        if ( nRead )
        {
            nRead = dbuf->Write(buf, nRead);
            total += nRead;
        }
    }
    while ( nRead );

    return total;
}

size_t wxStreamBuffer::Write(const void *buffer, size_t size)
{
    if (m_stream)
    {
        // lasterror is reset before all new IO calls
        m_stream->Reset();
    }

    size_t ret;

    if ( !HasBuffer() && m_fixed )
    {
        wxOutputStream *outStream = GetOutputStream();
        // no buffer, just forward the call to the stream
        ret = outStream->OnSysWrite(buffer, size);
    }
    else // we [may] have a buffer, use it
    {
        size_t orig_size = size;

        while ( size > 0 )
        {
            size_t left = GetBytesLeft();

            // if the buffer is too large to fit in the stream buffer, split
            // it in smaller parts
            //
            // NB: If stream buffer isn't fixed (as for wxMemoryOutputStream),
            //     we always go to the second case.
            //
            // FIXME: fine, but if it fails we should (re)try writing it by
            //        chunks as this will (hopefully) always work (VZ)

            if ( size > left && m_fixed )
            {
                PutToBuffer(buffer, left);
                size -= left;
                buffer = (char *)buffer + left;

                if ( !FlushBuffer() )
                {
                    SetError(wxSTREAM_WRITE_ERROR);

                    break;
                }

                m_buffer_pos = m_buffer_start;
            }
            else // we can do it in one gulp
            {
                PutToBuffer(buffer, size);
                size = 0;
            }
        }

        ret = orig_size - size;
    }

    if (m_stream)
    {
        // i am not entirely sure what we do this for
        m_stream->m_lastcount = ret;
    }

    return ret;
}

size_t wxStreamBuffer::Write(wxStreamBuffer *sbuf)
{
    char buf[BUF_TEMP_SIZE];
    size_t nWrite,
           total = 0;

    do
    {
        size_t nRead = sbuf->Read(buf, WXSIZEOF(buf));
        if ( nRead )
        {
            nWrite = Write(buf, nRead);
            if ( nWrite < nRead )
            {
                // put back data we couldn't copy
                wxInputStream *in_stream = (wxInputStream *)sbuf->GetStream();

                in_stream->Ungetch(buf + nWrite, nRead - nWrite);
            }

            total += nWrite;
        }
        else
        {
            nWrite = 0;
        }
    }
    while ( nWrite == WXSIZEOF(buf) );

    return total;
}

wxFileOffset wxStreamBuffer::Seek(wxFileOffset pos, wxSeekMode mode)
{
    wxFileOffset ret_off, diff;

    wxFileOffset last_access = GetLastAccess();

    if ( !m_flushable )
    {
        switch (mode)
        {
            case wxFromStart:
                diff = pos;
                break;

            case wxFromCurrent:
                diff = pos + GetIntPosition();
                break;

            case wxFromEnd:
                diff = pos + last_access;
                break;

            default:
                return wxInvalidOffset;
        }
        if (diff < 0 || diff > last_access)
            return wxInvalidOffset;
        size_t int_diff = wx_truncate_cast(size_t, diff);
        SetIntPosition(int_diff);
        return diff;
    }

    switch ( mode )
    {
        case wxFromStart:
            // We'll try to compute an internal position later ...
            ret_off = m_stream->OnSysSeek(pos, wxFromStart);
            ResetBuffer();
            return ret_off;

        case wxFromCurrent:
            diff = pos + GetIntPosition();

            if ( (diff > last_access) || (diff < 0) )
            {
                // We must take into account the fact that we have read
                // something previously.
                ret_off = m_stream->OnSysSeek(diff-last_access, wxFromCurrent);
                ResetBuffer();
                return ret_off;
            }
            else
            {
                size_t int_diff = wx_truncate_cast(size_t, diff);
                SetIntPosition(int_diff);
                return diff;
            }

        case wxFromEnd:
            // Hard to compute: always seek to the requested position.
            ret_off = m_stream->OnSysSeek(pos, wxFromEnd);
            ResetBuffer();
            return ret_off;
    }

    return wxInvalidOffset;
}

wxFileOffset wxStreamBuffer::Tell() const
{
    wxFileOffset pos;

    // ask the stream for position if we have a real one
    if ( m_stream )
    {
        pos = m_stream->OnSysTell();
        if ( pos == wxInvalidOffset )
            return wxInvalidOffset;
    }
    else // no associated stream
    {
        pos = 0;
    }

    pos += GetIntPosition();

    if ( m_mode == read && m_flushable )
        pos -= GetLastAccess();

    return pos;
}

// ----------------------------------------------------------------------------
// wxStreamBase
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxStreamBase, wxObject)

wxStreamBase::wxStreamBase()
{
    m_lasterror = wxSTREAM_NO_ERROR;
    m_lastcount = 0;
}

wxStreamBase::~wxStreamBase()
{
}

size_t wxStreamBase::GetSize() const
{
    wxFileOffset length = GetLength();
    if ( length == (wxFileOffset)wxInvalidOffset )
        return 0;

    const size_t len = wx_truncate_cast(size_t, length);
    return len;
}

wxFileOffset wxStreamBase::OnSysSeek(wxFileOffset WXUNUSED(seek), wxSeekMode WXUNUSED(mode))
{
    return wxInvalidOffset;
}

wxFileOffset wxStreamBase::OnSysTell() const
{
    return wxInvalidOffset;
}

// ----------------------------------------------------------------------------
// wxInputStream
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxInputStream, wxStreamBase)

wxInputStream::wxInputStream()
{
    m_wback = NULL;
    m_wbacksize =
    m_wbackcur = 0;
}

wxInputStream::~wxInputStream()
{
    free(m_wback);
}

bool wxInputStream::CanRead() const
{
    // we don't know if there is anything to read or not and by default we
    // prefer to be optimistic and try to read data unless we know for sure
    // there is no more of it
    return m_lasterror != wxSTREAM_EOF;
}

bool wxInputStream::Eof() const
{
    // the only way the base class can know we're at EOF is when we'd already
    // tried to read beyond it in which case last error is set accordingly
    return GetLastError() == wxSTREAM_EOF;
}

char *wxInputStream::AllocSpaceWBack(size_t needed_size)
{
    // get number of bytes left from previous wback buffer
    size_t toget = m_wbacksize - m_wbackcur;

    // allocate a buffer large enough to hold prev + new data
    char *temp_b = (char *)malloc(needed_size + toget);

    if (!temp_b)
        return NULL;

    // copy previous data (and free old buffer) if needed
    if (m_wback)
    {
        memmove(temp_b + needed_size, m_wback + m_wbackcur, toget);
        free(m_wback);
    }

    // done
    m_wback = temp_b;
    m_wbackcur = 0;
    m_wbacksize = needed_size + toget;

    return m_wback;
}

size_t wxInputStream::GetWBack(void *buf, size_t size)
{
    /* Clear buffer first */
    memset(buf, 0x00, size);

    if (!m_wback)
        return 0;

    // how many bytes do we have in the buffer?
    size_t toget = m_wbacksize - m_wbackcur;

    if ( size < toget )
    {
        // we won't read everything
        toget = size;
    }

    // copy the data from the cache
    memcpy(buf, m_wback + m_wbackcur, toget);

    m_wbackcur += toget;
    if ( m_wbackcur == m_wbacksize )
    {
        // TODO: should we really free it here all the time? maybe keep it?
        free(m_wback);
        m_wback = NULL;
        m_wbacksize = 0;
        m_wbackcur = 0;
    }

    // return the number of bytes copied
    return toget;
}

size_t wxInputStream::Ungetch(const void *buf, size_t bufsize)
{
	// can't operate on this stream until the error is cleared
    if ( m_lasterror != wxSTREAM_NO_ERROR && m_lasterror != wxSTREAM_EOF )
        return 0;

    char *ptrback = AllocSpaceWBack(bufsize);
    if (!ptrback)
        return 0;

    // Eof() shouldn't return true any longer
    if ( m_lasterror == wxSTREAM_EOF )
        m_lasterror = wxSTREAM_NO_ERROR;

    memcpy(ptrback, buf, bufsize);
    return bufsize;
}

bool wxInputStream::Ungetch(char c)
{
    return Ungetch(&c, sizeof(c)) != 0;
}

int wxInputStream::GetC()
{
    unsigned char c;
    Read(&c, sizeof(c));
    return LastRead() ? c : wxEOF;
}

wxInputStream& wxInputStream::Read(void *buf, size_t size)
{
    char *p = (char *)buf;
    m_lastcount = 0;

    size_t read = GetWBack(buf, size);
    for ( ;; )
    {
        size -= read;
        m_lastcount += read;
        p += read;

        if ( !size )
        {
            // we read the requested amount of data
            break;
        }

        if ( p != buf && !CanRead() )
        {
            // we have already read something and we would block in OnSysRead()
            // now: don't do it but return immediately
            break;
        }

        read = OnSysRead(p, size);
        if ( !read )
        {
            // no more data available
            break;
        }
    }

    return *this;
}

wxInputStream& wxInputStream::Read(wxOutputStream& stream_out)
{
    size_t lastcount = 0;
    char buf[BUF_TEMP_SIZE];

    for ( ;; )
    {
        size_t bytes_read = Read(buf, WXSIZEOF(buf)).LastRead();
        if ( !bytes_read )
            break;

        if ( stream_out.Write(buf, bytes_read).LastWrite() != bytes_read )
            break;

        lastcount += bytes_read;
    }

    m_lastcount = lastcount;

    return *this;
}

bool wxInputStream::ReadAll(void *buffer_, size_t size)
{
    char* buffer = static_cast<char*>(buffer_);

    size_t totalCount = 0;

    for ( ;; )
    {
        const size_t lastCount = Read(buffer, size).LastRead();

        // There is no point in continuing looping if we can't read anything at
        // all.
        if ( !lastCount )
            break;

        totalCount += lastCount;

        // ... Or if an error occurred on the stream.
        if ( !IsOk() )
            break;

        // Return successfully if we read exactly the requested number of
        // bytes (normally the ">" case should never occur and so we could use
        // "==" test, but be safe and avoid overflowing size even in case of
        // bugs in LastRead()).
        if ( lastCount >= size )
        {
            size = 0;
            break;
        }

        // Advance the buffer before trying to read the rest of data.
        size -= lastCount;
        buffer += lastCount;
    }

    m_lastcount = totalCount;

    return size == 0;
}

wxFileOffset wxInputStream::SeekI(wxFileOffset pos, wxSeekMode mode)
{
    // RR: This code is duplicated in wxBufferedInputStream. This is
    // not really a good design, but buffered stream are different
    // from all others in that they handle two stream-related objects:
    // the stream buffer and parent stream.

    // I don't know whether it should be put as well in wxFileInputStream::OnSysSeek
    if (m_lasterror==wxSTREAM_EOF)
        m_lasterror=wxSTREAM_NO_ERROR;

    // avoid unnecessary seek operations (optimization)
    wxFileOffset currentPos = TellI(), size = GetLength();
    if ((mode == wxFromStart && currentPos == pos) ||
        (mode == wxFromCurrent && pos == 0) ||
        (mode == wxFromEnd && size != wxInvalidOffset && currentPos == size-pos))
        return currentPos;

    if (!IsSeekable() && mode == wxFromCurrent && pos > 0)
    {
        // rather than seeking, we can just read data and discard it;
        // this allows to forward-seek also non-seekable streams!
        char buf[BUF_TEMP_SIZE];
        size_t bytes_read;

        // read chunks of BUF_TEMP_SIZE bytes until we reach the new position
        for ( ; pos >= BUF_TEMP_SIZE; pos -= bytes_read)
        {
            bytes_read = Read(buf, WXSIZEOF(buf)).LastRead();
            if ( m_lasterror != wxSTREAM_NO_ERROR )
                return wxInvalidOffset;
        }

        // read the last 'pos' bytes
        bytes_read = Read(buf, (size_t)pos).LastRead();
        if ( m_lasterror != wxSTREAM_NO_ERROR )
            return wxInvalidOffset;

        // we should now have sought to the right position...
        return TellI();
    }

    /* RR: A call to SeekI() will automatically invalidate any previous
       call to Ungetch(), otherwise it would be possible to SeekI() to
       one position, unread some bytes there, SeekI() to another position
       and the data would be corrupted.

       GRG: Could add code here to try to navigate within the wback
       buffer if possible, but is it really needed? It would only work
       when seeking in wxFromCurrent mode, else it would invalidate
       anyway... */

    if (m_wback)
    {
        free(m_wback);
        m_wback = NULL;
        m_wbacksize = 0;
        m_wbackcur = 0;
    }

    return OnSysSeek(pos, mode);
}

wxFileOffset wxInputStream::TellI() const
{
    wxFileOffset pos = OnSysTell();

    if (pos != wxInvalidOffset)
        pos -= (m_wbacksize - m_wbackcur);

    return pos;
}


// ----------------------------------------------------------------------------
// wxOutputStream
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxOutputStream, wxStreamBase)

wxOutputStream::wxOutputStream()
{
}

wxOutputStream::~wxOutputStream()
{
}

size_t wxOutputStream::OnSysWrite(const void * WXUNUSED(buffer),
                                  size_t WXUNUSED(bufsize))
{
    return 0;
}

void wxOutputStream::PutC(char c)
{
    Write(&c, sizeof(c));
}

wxOutputStream& wxOutputStream::Write(const void *buffer, size_t size)
{
    m_lastcount = OnSysWrite(buffer, size);
    return *this;
}

wxOutputStream& wxOutputStream::Write(wxInputStream& stream_in)
{
    stream_in.Read(*this);
    return *this;
}

bool wxOutputStream::WriteAll(const void *buffer_, size_t size)
{
    // This exactly mirrors ReadAll(), see there for more comments.
    const char* buffer = static_cast<const char*>(buffer_);

    size_t totalCount = 0;

    for ( ;; )
    {
        const size_t lastCount = Write(buffer, size).LastWrite();
        if ( !lastCount )
            break;

        totalCount += lastCount;

        if ( !IsOk() )
            break;

        if ( lastCount >= size )
        {
            size = 0;
            break;
        }

        size -= lastCount;
        buffer += lastCount;
    }

    m_lastcount = totalCount;
    return size == 0;
}

wxFileOffset wxOutputStream::TellO() const
{
    return OnSysTell();
}

wxFileOffset wxOutputStream::SeekO(wxFileOffset pos, wxSeekMode mode)
{
    return OnSysSeek(pos, mode);
}

void wxOutputStream::Sync()
{
}

#endif // wxUSE_STREAMS
