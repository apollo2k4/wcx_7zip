Utilty for Total Commander for testing WCX plugins.
Ver. 0.23

Implements algorithm described in "WCX Writer's Reference" to test plugin
functionality.

May be useful for testing plugins because plugin can write to stdout or stderr.
Or to work with archive with WCX plugin.

Usage:

    wcxtest [-l | | -t | -x] <wcx_path> <arc_path> [<dir_path>]

<wcx_path> - path to WCX plugin

<arc_path> - path to archive file

<dir_path> - directory to unpack files, default is current

        -f - list WCX exported functions

        -l - List archive contents (default)

        -t - Test archive contents

        -x - eXtract files from archive

    -?, -h - help

        -v - Verbose

ERRORLEVEL: 0 - success, non zero - some (m.b. unknown) error. See error
messages.

Switches are NOT case sensitive. It's order - arbitrary.

Program NOT tested with file names that contains non-ASCII chars.

--
History:

0.23 2010/12/16
    Migrated to Unicode.

0.22 2006/07/17:
	Added error message when plugin do not export NOT-optional functions
	SetChangeVolProc() and SetProcessDataProc().

0.2 beta:
    Program output made more usable.


--
Oleg Bondar <hobo-mts@mail.ru>,
Jul 2006.

Cristian Adam <cristian.adam@gmail.com>
Dec 2010.