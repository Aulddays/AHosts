/* ----------------------------------------------------------------------------
   libconfig - A library for processing structured configuration files
   Copyright (C) 2005-2018  Mark A Lindner

   This file is part of libconfig.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 3 of
   the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, see
   <http://www.gnu.org/licenses/>.
   ----------------------------------------------------------------------------
*/

#ifndef __libconfig_strbuf_h
#define __libconfig_strbuf_h

#include <string.h>
#include <sys/types.h>

typedef struct
{
  char *string;
  size_t length;
  size_t capacity;
} strbuf_t;

void strbuf_append_string(strbuf_t *buf, const char *s);

void strbuf_append_char(strbuf_t *buf, char c);

char *strbuf_release(strbuf_t *buf);

#endif /* __libconfig_strbuf_h */
