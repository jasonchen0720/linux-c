include $(JASON_WORK_DIR)/config.mk
include $(JASON_WORK_DIR)/cflags.mk

# executable program name, e.g. myprog 
EXECUTABLES :=

# static lib name, e.g. libmylib.a
STATIC_LIBS := libco.a

# shared lib name, e.g. libmylib.so
SHARED_LIBS :=

SRCS :=

CFLAGS +=

# Every subdirectory with source files must be described here
IFLAGS := \
-I.\
-I$(JASON_WORK_DIR)/lib/include \
-I$(JASON_WORK_DIR)/lib/tree \
-I$(JASON_WORK_DIR)/lib/memory-pool

# All of the sources participating in the build are defined here
SRCS += \
./co_switch.c \
./co_socket.c

ifeq ($(SAMPLE),SAMPLE)
SRCS += ./co_sample.c
endif

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

USER_OBJS := 



LIBS :=

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
