
# executable program name, e.g. myprog 
#EXECUTABLES :=

# static lib name, e.g. libmylib.a
STATIC_LIBS :=

# shared lib name, e.g. libmylib.so
SHARED_LIBS := libjason.so

SRCS :=

CFLAGS += -fPIC -Os -Wall -c -MMD -MP -Wno-unused-result #-fstack-protector-strong

# Every subdirectory with source files must be described here
IFLAGS := \
-I.\

IFLAGS += -I$(JASON_WORK_DIR)/base/lib/libipc/
IFLAGS += -I$(JASON_WORK_DIR)/lib/include/
# All of the sources participating in the build are defined here
SRCS += \
./ipcclient/client.c

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

USER_OBJS :=$(JASON_WORK_DIR)/base/lib/libipc/libipc.a

LIBS :=

SHARE_LIBS := -lpthread

SHARE_LDFLAGS :=

# All Target
all: $(STATIC_LIBS) $(SHARED_LIBS)

# Tool invocations
#$(EXECUTABLES): $(OBJS) $(USER_OBJS)
#	@echo 'Building target: $@'
#	$(CC) $(LDFLAGS) -o $(EXECUTABLES) $(OBJS) $(USER_OBJS) $(LIBS)
#	@echo 'Finished building target: $@'
#	@echo ' '

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
