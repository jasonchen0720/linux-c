C_SRCS += \
mc_tools.c

%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

