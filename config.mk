#
#Below are gcc options
#

#CROSS_COMPILE := 
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS :=
LDFLAGS :=

#CROSS_SYSROOT :=
ifneq ($(CROSS_SYSROOT),)
	CFLAGS += -march=armv7ve -mthumb -mfpu=neon -mfloat-abi=hard
	CFLAGS += --sysroot=$(CROSS_SYSROOT)
	LDFLAGS += -march=armv7ve -mthumb -mfpu=neon -mfloat-abi=hard
	LDFLAGS += --sysroot=$(CROSS_SYSROOT)
endif

#
#Below are features and configs
#
CONFIGS :=
CONFIGS += CONFIG_IPC
CONFIGS += CONFIG_TMR
CONFIGS += CONFIG_THREAD_POOL
$(foreach config,$(CONFIGS), \
	$(eval CFLAGS += -D$(config)) \
	$(eval $(config) = y)	\
)

DEFINITIONS :=
$(foreach definition,$(DEFINITIONS), \
	$(eval CFLAGS += -D$(definition)) \
)
