include $(JASON_WORK_DIR)/config.mk
include $(JASON_WORK_DIR)/cflags.mk

# executable program name, e.g. myprog 
EXECUTABLES := mcd

# static lib name, e.g. libmylib.a
STATIC_LIBS := 

# shared lib name, e.g. libmylib.so
SHARED_LIBS :=

C_SRCS :=

CFLAGS +=

# Every subdirectory with source files must be described here
IFLAGS :=

# All of the sources participating in the build are defined here
-include subdir.mk

OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

LIBS := -lpthread

IFLAGS += \
-I$(JASON_WORK_DIR)/include \
-I$(JASON_WORK_DIR)/lib/include \
-I$(JASON_WORK_DIR)/lib/ipc \

USER_OBJS :=

USER_OBJS = \
$(JASON_WORK_DIR)/lib/ipc/libipc.a \
$(JASON_WORK_DIR)/lib/cfg/libcfg.a \
$(JASON_WORK_DIR)/lib/common/libcommon.a \



# All Target
all: $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS)

# Tool invocations
$(EXECUTABLES): $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	$(CC) $(LDFLAGS) $(BOE_CFLAGS) -o $(EXECUTABLES) $(OBJS) $(USER_OBJS) $(LIBS)
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
	$(CC) $(LDFLAGS) -shared -o $(SHARED_LIBS) $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo  ' '

clean:
	-$(RM) $(OBJS) $(C_DEPS) $(EXECUTABLES) $(STATIC_LIBS) $(SHARED_LIBS)
	-@echo ' '

.PHONY: all clean
.SECONDARY:
