#ifndef __MC_IDENTITY_H__
#define __MC_IDENTITY_H__
enum MC_STATIC_IDENTITY {
	MC_IDENTITY_DYNAMIC		= -2,
	MC_IDENTITY_DUMMY		= -1,
	MC_IDENTITY_NV,					/* NV cache */
	MC_IDENTITY_BROKER,				/* Broker */
	MC_IDENTITY_MONITOR,
	MC_IDENTITY_MAX
};

#endif
