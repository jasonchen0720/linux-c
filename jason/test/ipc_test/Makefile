
# executable program name, e.g. myprog 
EXECUTABLES := test

# static lib name, e.g. libmylib.a
STATIC_LIBS :=

# shared lib name, e.g. libmylib.so
SHARED_LIBS :=

SRCS :=

CFLAGS += -fPIC -Os -Wall -c -MMD -MP -Wno-unused-result#-fstack-protector-strong -g

# Every subdirectory with source files must be described here
IFLAGS := \
-I.\

IFLAGS += -I$(JASON_WORK_DIR)/base/ipc/
IFLAGS += -I$(JASON_WORK_DIR)/lib/include/

#ld
LDFLAGS :=

LDFLAGS += -L$(JASON_WORK_DIR)/base/ipc/
LDFLAGS += -L$(JASON_WORK_DIR)/lib/
# All of the sources participating in the build are defined here
SRCS += \
./test_ipc.c

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

USER_OBJS :=$(JASON_WORK_DIR)/base/ipc/libipc.a
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
