#ifndef __MC_GUARD_TABLE_H__
#define __MC_GUARD_TABLE_H__
/*
 * Warning: only mc_guard.c using this header.
 *	Including this header in anyother source files should be prohibited.
 */
#define guard_maker(__id, __name, __cmdline, __pidfile) {.id = __id, .name = __name, .cmdline = __cmdline, .pidfile = __pidfile}
static struct mc_guard __mc_guard[] =
{
	/* Warning: Keep Asc order Via name */
	guard_maker(MC_IDENTITY_BROKER,  "broker", 		"broker", 				NULL),
	guard_maker(MC_IDENTITY_DUMMY,   "ipc-sample", 	"ipc-sample server", 	NULL),
	/* Warning:  Keep Asc order Via name */
};
#endif
