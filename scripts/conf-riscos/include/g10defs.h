/* g10defs.h - hand edited by Stefan Bellon to suit RISC OS needs
 *	Copyright (C) 2001 Free Software Foundation, Inc.
 *
 * This file is part of GNUPG.
 *
 * GNUPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GNUPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 */

/* Path variables and filing system constants for RISC OS */
#define G10_LOCALEDIR     "<GnuPG$Dir>.locale"
#define GNUPG_LIBDIR      "<GnuPG$Dir>"
#define GNUPG_LIBEXECDIR  "<GnuPG$Dir>"
#define GNUPG_DATADIR     "<GnuPG$Dir>"
#define GNUPG_HOMEDIR     "<GnuPGUser$Dir>"
#define LOCALE_ALIAS_PATH "<GnuPG$Dir>.locale"
#define GNULOCALEDIR      "<GnuPG$Dir>.locale"
#define DIRSEP_C            '.'
#define EXTSEP_C            '/'
#define DIRSEP_S            "."
#define EXTSEP_S            "/"
#define SAFE_VERSION_DOT    '/'
#define SAFE_VERSION_DASH   '-'

/* This file defines some basic constants for the MPI machinery.  We
 * need to define the types on a per-CPU basis, so it is done with
 * this file here.  */
#define BYTES_PER_MPI_LIMB  (SIZEOF_UNSIGNED_LONG)

/* External process spawning mechanism */
#if !(defined(HAVE_FORK) && defined(HAVE_PIPE) && defined(HAVE_WAITPID))
#define EXEC_TEMPFILE_ONLY 1
#endif
