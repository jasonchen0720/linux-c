SRCS += \
./bs_tree.c \
./rb_tree.c

SRCS += \
./generic_log.c \
./generic_bit.c \
./generic_file.c \
./generic_proc.c

SRCS += \
./ccl.c \
./gcl.c \
./scl.c


%.o: ./%.c
	@echo 'Building file: $<'
	$(CC) $(CFLAGS) $(IFLAGS) -I. -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

