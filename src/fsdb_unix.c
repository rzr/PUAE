 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Library of functions to make emulated filesystem as independent as
  * possible of the host filesystem's capabilities.
  * This is the Unix version.
  *
  * Copyright 1999 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "fsdb.h"

#define NUM_EVILCHARS 7
static TCHAR evilchars[NUM_EVILCHARS] = { '\\', '*', '?', '\"', '<', '>', '|' };

#define TRACING_ENABLED 0
#if TRACING_ENABLED
#define TRACE(x)	do { write_log x; } while(0)
#else
#define TRACE(x)
#endif

int dos_errno (void)
{
    int e = errno;

    switch (e) {
	case ENOMEM:	return ERROR_NO_FREE_STORE;
	case EEXIST:	return ERROR_OBJECT_EXISTS;
	case EACCES:	return ERROR_WRITE_PROTECTED;
	case ENOENT:	return ERROR_OBJECT_NOT_AROUND;
	case ENOTDIR:	return ERROR_OBJECT_WRONG_TYPE;
	case ENOSPC:	return ERROR_DISK_IS_FULL;
	case EBUSY:       	return ERROR_OBJECT_IN_USE;
	case EISDIR:	return ERROR_OBJECT_WRONG_TYPE;
#if defined(ETXTBSY)
	case ETXTBSY:	return ERROR_OBJECT_IN_USE;
#endif
#if defined(EROFS)
	case EROFS:       	return ERROR_DISK_WRITE_PROTECTED;
#endif
#if defined(ENOTEMPTY)
#if ENOTEMPTY != EEXIST
	case ENOTEMPTY:	return ERROR_DIRECTORY_NOT_EMPTY;
#endif
#endif
	default:
	TRACE (("FSDB: Unimplemented error: %s\n", strerror (e)));
	return ERROR_NOT_IMPLEMENTED;
    }
}

/* Return nonzero for any name we can't create on the native filesystem.  */
int fsdb_name_invalid (const char *n)
{
    if (strcmp (n, FSDB_FILE) == 0)
	return 1;
    if (n[0] != '.')
	return 0;
    if (n[1] == '\0')
	return 1;
    return n[1] == '.' && n[2] == '\0';
}

int fsdb_exists (const char *nname)
{
    struct stat statbuf;

    return (stat (nname, &statbuf) != -1);
}

/* For an a_inode we have newly created based on a filename we found on the
 * native fs, fill in information about this file/directory.  */
int fsdb_fill_file_attrs (a_inode *base, a_inode *aino)
{
    struct stat statbuf;
    /* This really shouldn't happen...  */
    if (stat (aino->nname, &statbuf) == -1)
	return 0;
    aino->dir = S_ISDIR (statbuf.st_mode) ? 1 : 0;
    aino->amigaos_mode = ((S_IXUSR & statbuf.st_mode ? 0 : A_FIBF_EXECUTE)
			  | (S_IWUSR & statbuf.st_mode ? 0 : A_FIBF_WRITE)
			  | (S_IRUSR & statbuf.st_mode ? 0 : A_FIBF_READ));
#ifdef ANDROID
    // Always give execute & read permission
    aino->amigaos_mode &= ~A_FIBF_EXECUTE;
    aino->amigaos_mode &= ~A_FIBF_READ;
#endif
    return 1;
}

int fsdb_set_file_attrs (a_inode *aino)
{
    struct stat statbuf;
    int mask = aino->amigaos_mode;
    int mode;

    if (stat (aino->nname, &statbuf) == -1)
	return ERROR_OBJECT_NOT_AROUND;

    mode = statbuf.st_mode;
    /* Unix dirs behave differently than AmigaOS ones.  */
    if (! aino->dir) {
	if (mask & A_FIBF_READ)
	    mode &= ~S_IRUSR;
	else
	    mode |= S_IRUSR;

	if (mask & A_FIBF_WRITE)
	    mode &= ~S_IWUSR;
	else
	    mode |= S_IWUSR;

	if (mask & A_FIBF_EXECUTE)
	    mode &= ~S_IXUSR;
	else
	    mode |= S_IXUSR;

	chmod (aino->nname, mode);
    }

    aino->amigaos_mode = mask;
    aino->dirty = 1;
    return 0;
}

/* return supported combination */
int fsdb_mode_supported (const a_inode *aino)
{
        int mask = aino->amigaos_mode;
        if (0 && aino->dir)
                return 0;
        if (fsdb_mode_representable_p (aino, mask))
                return mask;
        mask &= ~(A_FIBF_SCRIPT | A_FIBF_READ | A_FIBF_EXECUTE);
        if (fsdb_mode_representable_p (aino, mask))
                return mask;
        mask &= ~A_FIBF_WRITE;
        if (fsdb_mode_representable_p (aino, mask))
                return mask;
        mask &= ~A_FIBF_DELETE;
        if (fsdb_mode_representable_p (aino, mask))
                return mask;
        return 0;
}

/* Return nonzero if we can represent the amigaos_mode of AINO within the
 * native FS.  Return zero if that is not possible.  */
int fsdb_mode_representable_p (const a_inode *aino, int amigaos_mode)
{
        int mask = amigaos_mode ^ 15;

        if (0 && aino->dir)
                return amigaos_mode == 0;

        if (mask & A_FIBF_SCRIPT) /* script */
                return 0;
        if ((mask & 15) == 15) /* xxxxRWED == OK */
                return 1;
        if (!(mask & A_FIBF_EXECUTE)) /* not executable */
                return 0;
        if (!(mask & A_FIBF_READ)) /* not readable */
                return 0;
        if ((mask & 15) == (A_FIBF_READ | A_FIBF_EXECUTE)) /* ----RxEx == ReadOnly */
                return 1;
        return 0;
}

char *fsdb_create_unique_nname (a_inode *base, const char *suggestion)
{
	TCHAR *c;
	TCHAR tmp[256] = "__uae___";
	int i;

	strncat (tmp, suggestion, 240);

        /* replace the evil ones... */
        for (i = 0; i < NUM_EVILCHARS; i++)
                while ((c = _tcschr (tmp, evilchars[i])) != 0)
                        *c = '_';

        while ((c = _tcschr (tmp, '.')) != 0)
                *c = '_';
        while ((c = _tcschr (tmp, ' ')) != 0)
                *c = '_';

        for (;;) {
                TCHAR *p = build_nname (base->nname, tmp);
                if (!fsdb_exists (p)) {
                        write_log (_T("unique name: %s\n"), p);
                        return p;
                }
                xfree (p);
                /* tmpnam isn't reentrant and I don't really want to hack configure
                * right now to see whether tmpnam_r is available...  */
                for (i = 0; i < 8; i++) {
                        tmp[i+8] = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[rand () % 63];
                }
        }
}
