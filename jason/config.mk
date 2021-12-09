CROSS_COMPILE := 
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS :=

CONFIGS :=
CONFIGS += CONFIG_IPC
CONFIGS += CONFIG_TREE
CONFIGS += CONFIG_TMR
#CONFIGS += CONFIG_THREAD_POOL
$(foreach config,$(CONFIGS), \
	$(eval CFLAGS += -D$(config)) \
	$(eval $(config) = y)	\
)

DEFINITIONS :=
ifeq ($(CONFIG_THREAD_POOL),y)
DEFINITIONS += TMR_USE_THREAD_POOL
endif
$(foreach definition,$(DEFINITIONS), \
	$(eval CFLAGS += -D$(definition)) \
)