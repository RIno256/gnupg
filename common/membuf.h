/* membuf.h - A simple implementation of a dynamic buffer
 *	Copyright (C) 2001, 2003 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef GNUPG_COMMON_MEMBUF_H
#define GNUPG_COMMON_MEMBUF_H

/* The definition of the structure is private, we only need it here,
   so it can be allocated on the stack. */
struct private_membuf_s {
  size_t len;      
  size_t size;     
  char *buf;       
  int out_of_core; 
};

typedef struct private_membuf_s membuf_t;


void init_membuf (membuf_t *mb, int initiallen);
void put_membuf  (membuf_t *mb, const void *buf, size_t len);
void *get_membuf (membuf_t *mb, size_t *len);


#endif /*GNUPG_COMMON_MEMBUF_H*/