/* riscos.c -  RISC OS stuff
 *	Copyright (C) 2001 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG for RISC OS.
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

#ifndef __RISCOS__C__
#define __RISCOS__C__

#include <config.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <kernel.h>
#include <swis.h>
#include "util.h"
#include "memory.h"

#define __UNIXLIB_INTERNALS
#include <unixlib/unix.h>
#include <unixlib/swiparams.h>
#undef __UNIXLIB_INTERNALS

/* RISC OS file open descriptor control list */

struct fds_item {
    int fd;
    struct fds_item *next;
};
static struct fds_item *fds_list = NULL;
static int initialized = 0;


/* local RISC OS functions */

static int
is_read_only(const char *filename)
{
    int type, attr;
    
    if (_swix(OS_File, _INR(0,1) | _OUT(0) | _OUT(5),
              17, filename, &type, &attr))
        log_fatal("Can't get file attributes for %s!\n", filename);
    
    if (type == 0)
        log_fatal("Can't find file %s!\n", filename);

    if (_swix(OS_File, _INR(0,1) | _IN(5), 4, filename, attr))
        return 1;

    return 0;
}

static void
riscos_set_filetype_by_number(const char *filename, int type)
{
    if (_swix(OS_File, _INR(0,2), 18, filename, type))
        log_fatal("Can't set filetype for file %s!\n"
                  "Is the file on a read-only file system?\n", filename);
}        

/* exported RISC OS functions */

void
riscos_global_defaults(void)
{
    __riscosify_control = __RISCOSIFY_NO_PROCESS;
    __feature_imagefs_is_file = 1;
}

void
riscos_set_filetype(const char *filename, const char *mimetype)
{
    int result;

    if (_swix(MimeMap_Translate, _INR(0,2) | _OUT(3),
              MMM_TYPE_MIME, mimetype, MMM_TYPE_RISCOS, &result))
        log_fatal("Can't translate MIME type %s!\n", mimetype);

    riscos_set_filetype_by_number(filename, result);
}        

pid_t
riscos_getpid(void)
{
    int state;

    if (_swix(Wimp_ReadSysInfo, _IN(0) | _OUT(0), 3, &state))
        log_fatal("Wimp_ReadSysInfo failed: Can't get WimpState (R0=3)!\n");

    if (state)
        if (_swix(Wimp_ReadSysInfo, _IN(0) | _OUT(0), 5, &state))
            log_fatal("Wimp_ReadSysInfo failed: Can't get task handle (R0=5)!\n");

    return (pid_t) state;
}

int
riscos_kill(pid_t pid, int sig)
{
    int buf[4], iter = 0;

    if (sig)
        kill(pid, sig);

    do {
        if (_swix(TaskManager_EnumerateTasks, _INR(0,2) | _OUT(0),
                  iter, buf, 16, &iter))
            log_fatal("TaskManager_EnumerateTasks failed!\n");
        if (buf[0] == pid)
            return 0;
    } while (iter >= 0);

    return __set_errno(ESRCH);
}

int
riscos_access(const char *path, int amode)
{
    /* Do additional check, i.e. whether path is on write-protected floppy */
    if ((amode & W_OK) && is_read_only(path))
        return 1;
    return access(path, amode);
}

int
riscos_getchar(void)
{
    int c, flags;

    if (_swix(OS_ReadC, _OUT(0) | _OUT(_FLAGS), &c, &flags))
        log_fatal("OS_ReadC failed: Couldn't read from keyboard!\n");
    if (flags & _C)
        log_fatal("OS_ReadC failed: Return Code = %i!\n", c);

    return c;
}

#ifdef DEBUG
void
dump_fdlist(void)
{
    struct fds_item *iter = fds_list;
    printf("List of open file descriptors:\n");
    while (iter) {
        printf("  %i\n", iter->fd);
        iter = iter->next;
    }
}
#endif /* DEBUG */

int
fdopenfile(const char *filename, const int allow_write)
{
    struct fds_item *h;
    int fd;
    if (allow_write)
        fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    else
        fd = open(filename, O_RDONLY);
    if (fd == -1)
        log_error("Can't open file %s: %i, %s!\n", filename, errno, strerror(errno));

    if (!initialized) {
        atexit (close_fds);
        initialized = 1;
    }

    h = fds_list;
    fds_list = (struct fds_item *) m_alloc(sizeof(struct fds_item));
    fds_list->fd = fd;
    fds_list->next = h;

    return fd;
}

void
close_fds(void)
{
    FILE *fp;
    struct fds_item *h = fds_list;
    while( fds_list ) {
        h = fds_list->next;
        fp = fdopen (fds_list->fd, "a");
        if (fp)
            fflush(fp);
        close(fds_list->fd);
        m_free(fds_list);
        fds_list = h;
    }
}

int
renamefile(const char *old, const char *new)
{
    _kernel_oserror *e;

    if (e = _swix(OS_FSControl, _INR(0,2), 25, old, new)) {
        if (e->errnum == 214)
            return __set_errno(ENOENT);
        if (e->errnum == 176)
            return __set_errno(EEXIST);
        printf("Error during renaming: %i, %s!\n", e->errnum, e->errmess);
        return __set_errno(EOPSYS);
    }
    return 0;
}

char *
gstrans(const char *old)
{
    int size = 256, last;
    char *buf, *tmp;

    buf = (char *) m_alloc(size);
    if (!buf)
        log_fatal("Can't claim memory for OS_GSTrans buffer!\n");
    while (_C & _swi(OS_GSTrans, _INR(0,2) | _OUT(2) | _RETURN(_FLAGS),
                     old, buf, size, &last)) {
        size += 256;
        tmp = (char *) m_realloc(buf, size);
        if (!tmp)
             log_fatal("Can't claim memory for OS_GSTrans buffer!\n");
        buf = tmp;
    }

    buf[last] = '\0';
    tmp = (char *) m_realloc(buf, last + 1);
    if (!tmp)
        log_fatal("Can't realloc memory after OS_GSTrans!\n");

    return tmp;
}

#ifdef DEBUG
void
list_openfiles(void)
{
    char *name;
    int i, len;
    
    for (i = 255; i >= 0; --i) {
        if (_swix(OS_Args, _INR(0,2) | _IN(5) | _OUT(5), 7, i, 0, 0, &len))
            continue;

        name = (char *) m_alloc(1-len);
        if (!name)
            log_fatal("Can't claim memory for OS_Args buffer!\n");

        if (_swix(OS_Args, _INR(0,2) | _IN(5), 7, i, name, 1-len)) {
            m_free(name);
            log_fatal("Error when calling OS_Args(7)!\n");
        }
        
        if (_swix(OS_Args, _INR(0,1) | _OUT(0), 254, i, &len)) {
            m_free(name);
            log_fatal("Error when calling OS_Args(254)!\n");
        }
        
        printf("%3i: %s (%c%c)\n", i, name,
                                   (len & 0x40) ? 'R' : 0,
                                   (len & 0x80) ? 'W' : 0);
        m_free(name);
    }
}
#endif

void
not_implemented(const char *feature)
{
    log_info("%s is not implemented in the RISC OS version!\n", feature);
}

#endif /* !__RISCOS__C__ */