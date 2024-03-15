include $(PROJECT_ROOT)/config.mk
include $(PROJECT_ROOT)/cflags.mk

all clean:
	$(MAKE) -C ./tree  $@
ifeq ($(CONFIG_IPC),y)
	$(MAKE) -C ./ipc  $@
endif
ifeq ($(CONFIG_TMR),y)
	$(MAKE) -C ./timer  $@
endif
	$(MAKE) -C ./co  $@
