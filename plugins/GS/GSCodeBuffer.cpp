/*
 *	Copyright (C) 2007-2009 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <cstddef>

/* Forward declarations */
extern void* vmalloc(size_t size, bool code);
extern void vmfree(void* ptr, size_t size);

#include "GSCodeBuffer.h"

GSCodeBuffer::GSCodeBuffer(size_t blocksize)
	: m_blocksize(blocksize)
	, m_pos(0)
	, m_ptr(NULL)
{
}

GSCodeBuffer::~GSCodeBuffer()
{
	for(auto buffer : m_buffers)
		vmfree(buffer, m_blocksize);
}

void* GSCodeBuffer::GetBuffer(size_t size)
{
	size = (size + 15) & ~15;

	if(m_ptr == NULL || m_pos + size > m_blocksize)
	{
		m_ptr = (u8*)vmalloc(m_blocksize, true);
		m_pos = 0;
		m_buffers.push_back(m_ptr);
		return &m_ptr[0];
	}

	return &m_ptr[m_pos];
}

void GSCodeBuffer::ReleaseBuffer(size_t size)
{
	m_pos = ((m_pos + size) + 15) & ~15;
}
