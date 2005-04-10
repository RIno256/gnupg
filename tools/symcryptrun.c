/* symcryptrun.c - Tool to call simple symmetric encryption tools.
 *	Copyright (C) 2005 Free Software Foundation, Inc.
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


/* Sometimes simple encryption tools are already in use for a long
   time and there is a desire to integrate them into the GnuPG
   framework.  The protocols and encryption methods might be
   non-standard or not even properly documented, so that a
   full-fledged encryption tool with an interface like gpg is not
   doable.  This simple wrapper program provides a solution: It
   operates by calling the encryption/decryption module and providing
   the passphrase for a key (or even the key directly) using the
   standard pinentry mechanism through gpg-agent.  */

/* This program is invoked in the following way:

   symcryptrun --class CLASS --program PROGRAM --keyfile KEYFILE \
     [--decrypt | --encrypt]

   For encryption, the plain text must be provided on STDIN, and the
   ciphertext will be output to STDOUT.  For decryption vice versa.

   CLASS can currently only be "confucius".

   PROGRAM must be the path to the crypto engine.

   KEYFILE must contain the secret key, which may be protected by a
   passphrase.  The passphrase is retrieved via the pinentry program.


   The GPG Agent _must_ be running before starting symcryptrun.

   The possible exit status codes:

   0	Success
   1	Some error occured
   2	No valid passphrase was provided
   3	The operation was canceled by the user

   Other classes may be added in the future.  */


#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pty.h>
#include <utmp.h>
#include <ctype.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif
#include <gpg-error.h>

#define JNLIB_NEED_LOG_LOGV
#include "i18n.h"
#include "../common/util.h"

/* FIXME: Bah.  For spwq_secure_free.  */
#define SIMPLE_PWQUERY_IMPLEMENTATION 1
#include "../common/simple-pwquery.h"


/* Used by gcry for logging */
static void
my_gcry_logger (void *dummy, int level, const char *fmt, va_list arg_ptr)
{
  /* translate the log levels */
  switch (level)
    {
    case GCRY_LOG_CONT: level = JNLIB_LOG_CONT; break;
    case GCRY_LOG_INFO: level = JNLIB_LOG_INFO; break;
    case GCRY_LOG_WARN: level = JNLIB_LOG_WARN; break;
    case GCRY_LOG_ERROR:level = JNLIB_LOG_ERROR; break;
    case GCRY_LOG_FATAL:level = JNLIB_LOG_FATAL; break;
    case GCRY_LOG_BUG:  level = JNLIB_LOG_BUG; break;
    case GCRY_LOG_DEBUG:level = JNLIB_LOG_DEBUG; break;
    default:            level = JNLIB_LOG_ERROR; break;      }
  log_logv (level, fmt, arg_ptr);
}


/* Constants to identify the commands and options. */
enum cmd_and_opt_values
  {
    aNull = 0,
    oQuiet      = 'q',
    oVerbose	= 'v',

    oNoVerbose	= 500,
    oLogFile,
    oHomedir,
    oClass,
    oProgram,
    oKeyfile,
    oDecrypt,
    oEncrypt,
  };


/* The list of commands and options.  */
static ARGPARSE_OPTS opts[] =
  {
    { 301, NULL, 0, N_("@\nCommands:\n ") },

    { oDecrypt, "decrypt", 0, N_("decryption modus")},
    { oEncrypt, "encrypt", 0, N_("encryption modus")},
    
    { 302, NULL, 0, N_("@\nOptions:\n ") },
    
    { oClass, "class", 2, N_("tool class (confucius)")},
    { oProgram, "program", 2, N_("program filename")},

    { oKeyfile, "keyfile", 2, N_("secret key file (required)")},

    { oVerbose, "verbose",  0, N_("verbose") },
    { oQuiet, "quiet",      0, N_("quiet") },
    { oLogFile, "log-file", 2, N_("use a log file for the server")},

    /* Hidden options.  */
    { oNoVerbose, "no-verbose",  0, "@"},
    { oHomedir, "homedir", 2, "@" },   

    {0}
  };


/* We keep all global options in the structure OPT.  */
struct
{
  int verbose;		/* Verbosity level.  */
  int quiet;		/* Be extra quiet.  */
  const char *homedir;  /* Configuration directory name */

  char *class;
  char *program;
  char *keyfile;
} opt;


/* Print usage information and and provide strings for help.  */
static const char *
my_strusage (int level)
{
  const char *p;

  switch (level)
    {
    case 11: p = "symcryptrun (GnuPG)";
      break;
    case 13: p = VERSION; break;
    case 17: p = PRINTABLE_OS_NAME; break;
    case 19: p = _("Please report bugs to <" PACKAGE_BUGREPORT ">.\n");
      break;
    case 1:
    case 40: p = _("Usage: symcryptrun [options] (-h for help)");
      break;
    case 41:
      p = _("Syntax: symcryptrun --class CLASS --program PROGRAM "
	    "--keyfile KEYFILE [options...] COMMAND\n"
            "Call a simple symmetric encryption tool\n");
      break;
    case 31: p = "\nHome: "; break;
    case 32: p = opt.homedir; break;
    case 33: p = "\n"; break;

    default: p = NULL; break;
    }
  return p;
}


/* Initialize the gettext system.  */
static void
i18n_init(void)
{
#ifdef USE_SIMPLE_GETTEXT
  set_gettext_file (PACKAGE_GT);
#else
# ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE_GT, LOCALEDIR);
  textdomain (PACKAGE_GT);
# endif
#endif
}


/* Class Confucius.

   "Don't worry that other people don't know you;
   worry that you don't know other people."            Analects--1.16.  */

/* Create temporary directory with mode 0700.  Returns a dynamically
   allocated string with the filename of the directory.  */
static char *
confucius_mktmpdir (void)
{
  int res;
  char *tmpdir;

  tmpdir = tmpnam (NULL);
  if (!tmpdir)
    {
      log_error (_("cannot create temporary directory name: %s\n"),
		 strerror (errno));
      return NULL;
    }
  tmpdir = strdup (tmpdir);
  if (!tmpdir)
    {
      log_error (_("cannot copy temporary directory name: %s\n"),
		 strerror (errno));
      return NULL;
    }
  res = mkdir (tmpdir, 0700);
  if (res < 0)
    {
      log_error (_("cannot create temporary directory %s: %s\n"),
		 tmpdir, strerror (errno));
      return NULL;
    }

  return tmpdir;
}


/* Buffer size for I/O operations.  */
#define CONFUCIUS_BUFSIZE 4096

/* Buffer size for output lines.  */
#define CONFUCIUS_LINESIZE 4096


/* Copy the file IN to OUT, either of which may be "-".  */
static int
confucius_copy_file (const char *infile, const char *outfile)
{
  FILE *in;
  int in_is_stdin = 0;
  FILE *out;
  int out_is_stdout = 0;
  char data[CONFUCIUS_BUFSIZE];
  ssize_t data_len;

  if (infile[0] == '-' && infile[1] == '\0')
    {
      /* FIXME: Is stdin in binary mode?  */
      in = stdin;
      in_is_stdin = 1;
    }
  else
    {
      in = fopen (infile, "rb");
      if (!in)
	{
	  log_error (_("could not open %s for writing: %s\n"),
		     infile, strerror (errno));
	  return 1;
	}
    }

  if (outfile[0] == '-' && outfile[1] == '\0')
    {
      /* FIXME: Is stdout in binary mode?  */
      out = stdout;
      out_is_stdout = 1;
    }
  else
    {
      out = fopen (outfile, "wb");
      if (!out)
	{
	  log_error (_("could not open %s for writing: %s\n"),
		     infile, strerror (errno));
	  return 1;
	}
    }

  /* Now copy the data.  */
  while ((data_len = fread (data, 1, sizeof (data), in)) > 0)
    {
      if (fwrite (data, 1, data_len, out) != data_len)
	{
	  log_error (_("error writing to %s: %s\n"), outfile,
		     strerror (errno));
	  goto copy_err;
	}
    }
  if (data_len < 0 || ferror (in))
    {
      log_error (_("error reading from %s: %s\n"), infile, strerror (errno));
      goto copy_err;
    }

  /* Close IN if appropriate.  */
  if (!in_is_stdin && fclose (in) && ferror (in))
    {
      log_error (_("error closing %s: %s\n"), infile, strerror (errno));
      goto copy_err;
    }

  /* Close OUT if appropriate.  */
  if (!out_is_stdout && fclose (out) && ferror (out))
    {
      log_error (_("error closing %s: %s\n"), infile, strerror (errno));
      goto copy_err;
    }

  return 0;

 copy_err:
  if (!out_is_stdout)
    unlink (outfile);
  return 1;
}


/* Get a passphrase in secure storage (if possible).  If AGAIN is
   true, then this is a repeated attempt.  If CANCELED is not a null
   pointer, it will be set to true or false, depending on if the user
   canceled the operation or not.  On error (including cancelation), a
   null pointer is returned.  The passphrase must be deallocated with
   confucius_drop_pass.  */
char *
confucius_get_pass (int again, int *canceled)
{
  int err;
  char *pw;
#ifdef HAVE_LANGINFO_CODESET
  char *orig_codeset = NULL;
#endif

  if (canceled)
    *canceled = 0;
  
#ifdef ENABLE_NLS
  /* The Assuan agent protocol requires us to transmit utf-8 strings */
  orig_codeset = bind_textdomain_codeset (PACKAGE_GT, NULL);
#ifdef HAVE_LANGINFO_CODESET
  if (!orig_codeset)
    orig_codeset = nl_langinfo (CODESET);
#endif
  if (orig_codeset && !strcmp (orig_codeset, "UTF-8"))
    orig_codeset = NULL;
  if (orig_codeset)
    {
      /* We only switch when we are able to restore the codeset later. */
      orig_codeset = xstrdup (orig_codeset);
      if (!bind_textdomain_codeset (PACKAGE_GT, "utf-8"))
        orig_codeset = NULL; 
    }
#endif

  pw = simple_pwquery (NULL,
                       again ? _("does not match - try again"):NULL,
                       _("Passphrase:"), NULL, &err);

#ifdef ENABLE_NLS
  if (orig_codeset)
    {
      bind_textdomain_codeset (PACKAGE_GT, orig_codeset);
      xfree (orig_codeset);
    }
#endif

  if (!pw)
    {
      if (err)
        log_error (_("error while asking for the passphrase: %s\n"),
                   gpg_strerror (err));
      else
        {
	  log_info (_("cancelled\n"));
	  if (canceled)
	    *canceled = 1;
	}      
    }

  return pw;
}


/* Drop a passphrase retrieved with confucius_get_pass.  */
void
confucius_drop_pass (char *pass)
{
  if (pass)
    spwq_secure_free (pass);
}


/* Run a confucius crypto engine.  If MODE is oEncrypt, encryption is
   requested.  If it is oDecrypt, decryption is requested.  INFILE and
   OUTFILE are the temporary files used in the process.  */
int
confucius_process (int mode, char *infile, char *outfile)
{
  char *const args[] = { opt.program,
			 mode == oEncrypt ? "-m1" : "-m2",
			 "-q", infile,
			 "-z", outfile,
			 "-s", opt.keyfile,
			 mode == oEncrypt ? "-af" : "-f",
			 NULL };
  int cstderr[2];
  int master;
  int slave;
  int res;
  pid_t pid;
  pid_t wpid;
  int tries = 0;

  signal (SIGPIPE, SIG_IGN);

  if (!opt.program)
    {
      log_error (_("no --program option provided\n"));
      return 1;
    }

  if (mode != oDecrypt && mode != oEncrypt)
    {
      log_error (_("only --decrypt and --encrypt are supported\n"));
      return 1;
    }

  if (!opt.keyfile)
    {
      log_error (_("no --keyfile option provided\n"));
      return 1;
    }

  if (pipe (cstderr) < 0)
    {
      log_error (_("could not create pipe: %s\n"), strerror (errno));
      return 1;
    }

  if (openpty (&master, &slave, NULL, NULL, NULL) == -1)
    {
      log_error (_("could not create pty: %s\n"), strerror (errno));
      close (cstderr[0]);
      close (cstderr[1]);
      return -1;
    }

  /* We don't want to deal with the worst case scenarios.  */
  assert (master > 2);
  assert (slave > 2);
  assert (cstderr[0] > 2);
  assert (cstderr[1] > 2);

  pid = fork ();
  if (pid < 0)
    {
      log_error (_("could not fork: %s\n"), strerror (errno));
      close (master);
      close (slave);
      close (cstderr[0]);
      close (cstderr[1]);
      return 1;
    }
  else if (pid == 0) 
    {
      /* Child.  */

      /* Close the parent ends.  */
      close (master);
      close (cstderr[0]);

      /* Change controlling terminal.  */
      if (login_tty (slave))
	{
	  /* It's too early to output a debug message.  */
	  _exit (1);
	}

      dup2 (cstderr[1], 2);
      close (cstderr[1]);

      /* Now kick off the engine program.  */
      execv (opt.program, args);
      log_error (_("execv failed: %s\n"), strerror (errno));
      _exit (1);
    }
  else
    {
      /* Parent.  */
      char buffer[CONFUCIUS_LINESIZE];
      int buffer_len = 0;
      fd_set fds;
      int slave_closed = 0;
      int stderr_closed = 0;

      close (slave);
      close (cstderr[1]);

      /* Listen on the output FDs.  */
      do
	{
	  FD_ZERO (&fds);

	  if (!slave_closed)
	    FD_SET (master, &fds);
	  if (!stderr_closed)
	    FD_SET (cstderr[0], &fds);

	  res = select (FD_SETSIZE, &fds, NULL, NULL, NULL);
	  if (res < 0)
	    {
	      log_error (_("select failed: %s\n"), strerror (errno));

	      kill (pid, SIGTERM);
	      close (master);
	      close (cstderr[0]);
     	      return 1;
	    }

	  if (FD_ISSET (cstderr[0], &fds))
	    {
	      /* We got some output on stderr.  This is just passed
		 through via the logging facility.  */

	      res = read (cstderr[0], &buffer[buffer_len],
			  sizeof (buffer) - buffer_len - 1);
	      if (res < 0)
		{
		  log_error (_("read failed: %s\n"), strerror (errno));

		  kill (pid, SIGTERM);
		  close (master);
		  close (cstderr[0]);
		  return 1;
		}
	      else  
		{
		  char *newline;

		  buffer_len += res;
		  for (;;)
		    {
		      buffer[buffer_len] = '\0';
		      newline = strchr (buffer, '\n');
		      if (newline)
			{
			  *newline = '\0';
			  log_error ("%s\n", buffer);
			  buffer_len -= newline + 1 - buffer;
			  memmove (buffer, newline + 1, buffer_len);
			}
		      else if (buffer_len == sizeof (buffer) - 1)
			{
			  /* Overflow.  */
			  log_error ("%s\n", buffer);
			  buffer_len = 0;
			}
		      else
			break;
		    }

		  if (res == 0)
		    stderr_closed = 1;
		}
	    }
	  else if (FD_ISSET (master, &fds))
	    {
	      char data[512];

	      res = read (master, data, sizeof (data));
	      if (res < 0)
		{
		  if (errno == EIO)
		    {
		      /* Slave-side close leads to readable fd and
			 EIO.  */
		      slave_closed = 1;
		    }
		  else
		    {
		      log_error (_("pty read failed: %s\n"), strerror (errno));

		      kill (pid, SIGTERM);
		      close (master);
		      close (cstderr[0]);
		      return 1;
		    }
		}
	      else if (res == 0)
		/* This never seems to be what happens on slave-side
		   close.  */
		slave_closed = 1;
	      else
		{
		  /* Check for password prompt.  */
		  if (data[res - 1] == ':')
		    {
		      char *pass;
		      int canceled;

		      pass = confucius_get_pass (tries ? 1 : 0, &canceled);
		      if (!pass)
			{
			  kill (pid, SIGTERM);
			  close (master);
			  close (cstderr[0]);
			  return canceled ? 3 : 1;
			}
 		      write (master, pass, strlen (pass));
 		      write (master, "\n", 1);
		      confucius_drop_pass (pass);

		      tries++;
		    }
		}
	    }
	}
      while (!stderr_closed || !slave_closed);

      close (master);
      close (cstderr[0]);

      wpid = waitpid (pid, &res, 0);
      if (wpid < 0)
	{
	  log_error (_("waitpid failed: %s\n"), strerror (errno));

	  kill (pid, SIGTERM);
	  return 1;
	}
      else
	{
	  /* Shouldn't happen, as we don't use WNOHANG.  */
	  assert (wpid != 0);

	  if (!WIFEXITED (res))
	    {
	      log_error (_("child aborted with status %i\n"), res);
	      return 1;
	    }

	  if (WEXITSTATUS (res))
	    {
	      /* We probably exceeded our number of attempts at guessing
		 the password.  */
	      if (tries >= 3)
		return 2;
	      else
		return 1;
	    }

	  return 0;
	}
    }

  /* Not reached.  */
}


/* Class confucius main program.  If MODE is oEncrypt, encryption is
   requested.  If it is oDecrypt, decryption is requested.  The other
   parameters are taken from the global option data.  */
int
confucius_main (int mode)
{
  int res;
  char *tmpdir;
  char *infile;
  char *outfile;

  tmpdir = confucius_mktmpdir ();
  if (!tmpdir)
    return 1;

  /* TMPDIR + "/" + "in" + "\0".  */
  infile = malloc (strlen (tmpdir) + 1 + 2 + 1);
  if (!infile)
    {
      log_error (_("cannot allocate infile string: %s\n"), strerror (errno));
      rmdir (tmpdir);
      return 1;
    }
  strcpy (infile, tmpdir);
  strcat (infile, "/in");

  /* TMPDIR + "/" + "out" + "\0".  */
  outfile = malloc (strlen (tmpdir) + 1 + 3 + 1);
  if (!outfile)
    {
      log_error (_("cannot allocate outfile string: %s\n"), strerror (errno));
      free (infile);
      rmdir (tmpdir);
      return 1;
    }
  strcpy (outfile, tmpdir);
  strcat (outfile, "/out");

  /* Create INFILE and fill it with content.  */
  res = confucius_copy_file ("-", infile);
  if (res)
    {
      free (outfile);
      free (infile);
      rmdir (tmpdir);
      return res;
    }

  /* Run the engine and thus create the output file, handling
     passphrase retrieval.  */
  res = confucius_process (mode, infile, outfile);
  if (res)
    {
      unlink (outfile);
      unlink (infile);
      free (outfile);
      free (infile);
      rmdir (tmpdir);
      return res;
    }

  /* Dump the output file to stdout.  */
  res = confucius_copy_file (outfile, "-");
  if (res)
    {
      unlink (outfile);
      unlink (infile);
      free (outfile);
      free (infile);
      rmdir (tmpdir);
      return res;
    }
  
  unlink (outfile);
  unlink (infile);
  free (outfile);
  free (infile);
  rmdir (tmpdir);
  return 0;
}


/* symcryptrun's entry point.  */
int
main (int argc, char **argv)
{
  ARGPARSE_ARGS pargs;
  int no_more_options = 0;
  int mode = 0;
  int res;
  char *logfile = NULL;

  set_strusage (my_strusage);
  log_set_prefix ("symcryptrun", 1);

  /* Try to auto set the character set.  */
  set_native_charset (NULL); 

  i18n_init();

  opt.homedir = default_homedir ();

  /* Parse the command line. */
  pargs.argc  = &argc;
  pargs.argv  = &argv;
  pargs.flags =  1;  /* Do not remove the args.  */
  while (!no_more_options && optfile_parse (NULL, NULL, NULL, &pargs, opts))
    {
      switch (pargs.r_opt)
        {
        case oDecrypt:   mode = oDecrypt; break;
        case oEncrypt:   mode = oEncrypt; break;

	case oQuiet:     opt.quiet = 1; break;
        case oVerbose:   opt.verbose++; break;
        case oNoVerbose: opt.verbose = 0; break;
        case oHomedir:   opt.homedir = pargs.r.ret_str; break;
	  
	case oClass:	opt.class = pargs.r.ret_str; break;
	case oProgram:	opt.program = pargs.r.ret_str; break;
	case oKeyfile:	opt.keyfile = pargs.r.ret_str; break;

        case oLogFile:  logfile = pargs.r.ret_str; break;

        default: pargs.err = 2; break;
	}
    }

  if (!mode)
    log_error (_("either %s or %s must be given\n"),
               "--decrypt", "--encrypt");

  if (log_get_errorcount (0))
    exit (1);

  if (logfile)
    log_set_file (logfile);

  gcry_control (GCRYCTL_SUSPEND_SECMEM_WARN);
  if (!gcry_check_version (NEED_LIBGCRYPT_VERSION) )
    {
      log_fatal( _("libgcrypt is too old (need %s, have %s)\n"),
                 NEED_LIBGCRYPT_VERSION, gcry_check_version (NULL) );
    }
  gcry_set_log_handler (my_gcry_logger, NULL);
  gcry_control (GCRYCTL_INIT_SECMEM, 16384, 0);

  if (!opt.class)
    {
      log_error (_("no class provided\n"));
      res = 1;
    }
  else if (!strcmp (opt.class, "confucius"))
    res = confucius_main (mode);
  else
    {
      log_error (_("class %s is not supported\n"), opt.class);
      res = 1;
    }

  return res;
}