include $(JASON_WORK_DIR)/config.mk
include $(JASON_WORK_DIR)/cflags.mk

# executable program name, e.g. myprog 
EXECUTABLES := test

# static lib name, e.g. libmylib.a
STATIC_LIBS :=

# shared lib name, e.g. libmylib.so
SHARED_LIBS :=

SRCS :=

CFLAGS +=

# Every subdirectory with source files must be described here
IFLAGS := \
-I.\
-I$(JASON_WORK_DIR)/include \

ifeq ($(CONFIG_IPC),y)
IFLAGS += -I$(JASON_WORK_DIR)/lib/ipc
endif
ifeq ($(CONFIG_TREE),y)
IFLAGS += -I$(JASON_WORK_DIR)/lib/tree
endif
ifeq ($(CONFIG_TMR),y)
IFLAGS += -I$(JASON_WORK_DIR)/lib/timer
endif

#ld
LDFLAGS :=

LDFLAGS += -L$(JASON_WORK_DIR)/lib/ipc
LDFLAGS += -L$(JASON_WORK_DIR)/lib
# All of the sources participating in the build are defined here
SRCS += \
./test.c
ifeq ($(CONFIG_IPC),y)
SRCS += test_ipc.c
endif
ifeq ($(CONFIG_TREE),y)
SRCS += test_tree.c
endif
ifeq ($(CONFIG_TMR),y)
SRCS += test_timer.c
endif

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

USER_OBJS := \
$(JASON_WORK_DIR)/lib/common/libbase.a

ifeq ($(CONFIG_IPC),y)
USER_OBJS += $(JASON_WORK_DIR)/lib/ipc/libipc.a
endif
ifeq ($(CONFIG_TREE),y)
USER_OBJS += $(JASON_WORK_DIR)/lib/cfg/libcfg.a $(JASON_WORK_DIR)/lib/tree/libtree.a
endif
ifeq ($(CONFIG_TMR),y)
USER_OBJS += $(JASON_WORK_DIR)/lib/timer/libtm.a
endif
ifeq ($(CONFIG_THREAD_POOL),y)
USER_OBJS += $(JASON_WORK_DIR)/lib/thread-pool/libthread-pool.a
endif
LIBS := -ljason -lpthread

SHARE_LIBS :=

SHARE_LDFLAGS :=

# All Target
all: $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS)

# Tool invocations
$(EXECUTABLES): $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(CC) $(LDFLAGS) -o $(EXECUTABLES) $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Tool invocations
$(STATIC_LIBS): $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(AR) rcs $(STATIC_LIBS) $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Tool invocations
$(SHARED_LIBS): $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(CC) $(SHARE_LDFLAGS) -shared -o $(SHARED_LIBS) $(OBJS) $(USER_OBJS) $(SHARE_LIBS)
	@echo 'Finished building target: $@'
	@echo  ' '

clean:
	-$(RM) $(OBJS) $(DEPS) $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS)
	-@echo ' '

.PHONY: all clean
.SECONDARY:
