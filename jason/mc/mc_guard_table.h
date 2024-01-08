#ifndef __MC_GUARD_TABLE_H__
#define __MC_GUARD_TABLE_H__
/*
 * Warning: only mc_guard.c using this header.
 *	Including this header in anyother source files should be prohibited.
 */
static struct mc_guard __mc_guard[] =
{
	/* Warning: Keep Asc order Via name */
	{.id = MC_IDENTITY_BROKER,		.pid = 0, .name = "broker",	 		 .cmdline = "broker"},
	
	//{.id = MC_IDENTITY_MONITOR,	    .pid = 0, .name = "monitor", 		 .cmdline = "monitor -c /app/config/monitor.conf"},
	//{.id = MC_IDENTITY_NV,			.pid = 0, .name = "nv_service",		 .cmdline = "nv_service"},
	/* Warning:  Keep Asc order Via name */
};
#endif
