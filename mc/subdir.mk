C_SRCS += \
mc_core.c \
mc_guard.c \
mc_config.c \
mc_watchdog.c \
mc_exception.c \
mc_system.c \
mc_utils.c

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

