include $(PROJECT_ROOT)/config.mk
include $(PROJECT_ROOT)/cflags.mk

all clean:
	$(MAKE) -C ./mc  $@
	$(MAKE) -C ./broker  $@
	$(MAKE) -C ./tree-sample  $@
ifeq ($(CONFIG_IPC),y)
	$(MAKE) -C ./ipc-sample  $@
endif
ifeq ($(CONFIG_TMR),y)
	$(MAKE) -C ./timer-sample  $@
endif
	$(MAKE) -C ./co-sample  $@
