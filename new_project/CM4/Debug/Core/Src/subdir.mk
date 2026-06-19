################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
<<<<<<< HEAD
../Core/Src/adc.c \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
../Core/Src/dma.c \
../Core/Src/freertos.c \
../Core/Src/gpio.c \
../Core/Src/ipcc.c \
../Core/Src/main.c \
<<<<<<< HEAD
../Core/Src/rpmsg_handler.c \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
../Core/Src/stm32mp1xx_hal_msp.c \
../Core/Src/stm32mp1xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
<<<<<<< HEAD
../Core/Src/task_buzzer.c \
../Core/Src/task_led.c \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
../Core/Src/tim.c \
../Core/Src/usart.c \
../Core/Src/ws2812b.c 

OBJS += \
<<<<<<< HEAD
./Core/Src/adc.o \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
./Core/Src/dma.o \
./Core/Src/freertos.o \
./Core/Src/gpio.o \
./Core/Src/ipcc.o \
./Core/Src/main.o \
<<<<<<< HEAD
./Core/Src/rpmsg_handler.o \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
./Core/Src/stm32mp1xx_hal_msp.o \
./Core/Src/stm32mp1xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
<<<<<<< HEAD
./Core/Src/task_buzzer.o \
./Core/Src/task_led.o \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
./Core/Src/tim.o \
./Core/Src/usart.o \
./Core/Src/ws2812b.o 

C_DEPS += \
<<<<<<< HEAD
./Core/Src/adc.d \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
./Core/Src/dma.d \
./Core/Src/freertos.d \
./Core/Src/gpio.d \
./Core/Src/ipcc.d \
./Core/Src/main.d \
<<<<<<< HEAD
./Core/Src/rpmsg_handler.d \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
./Core/Src/stm32mp1xx_hal_msp.d \
./Core/Src/stm32mp1xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
<<<<<<< HEAD
./Core/Src/task_buzzer.d \
./Core/Src/task_led.d \
=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
./Core/Src/tim.d \
./Core/Src/usart.d \
./Core/Src/ws2812b.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DMETAL_MAX_DEVICE_REGIONS=2 -DSTM32MP157Dxx -DUSE_HAL_DRIVER -DCORE_CM4 -DDEBUG -DNO_ATOMIC_64_SUPPORT -DMETAL_INTERNAL -DVIRTIO_SLAVE_ONLY -c -I../Core/Inc -I../../Drivers/STM32MP1xx_HAL_Driver/Inc -I../../Drivers/STM32MP1xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32MP1xx/Include -I../../Drivers/CMSIS/Include -I../OPENAMP -I../../Middlewares/Third_Party/OpenAMP/open-amp/lib/include -I../../Middlewares/Third_Party/OpenAMP/libmetal/lib/include -I../../Middlewares/Third_Party/OpenAMP/virtual_driver -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
<<<<<<< HEAD
	-$(RM) ./Core/Src/adc.cyclo ./Core/Src/adc.d ./Core/Src/adc.o ./Core/Src/adc.su ./Core/Src/dma.cyclo ./Core/Src/dma.d ./Core/Src/dma.o ./Core/Src/dma.su ./Core/Src/freertos.cyclo ./Core/Src/freertos.d ./Core/Src/freertos.o ./Core/Src/freertos.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/ipcc.cyclo ./Core/Src/ipcc.d ./Core/Src/ipcc.o ./Core/Src/ipcc.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/rpmsg_handler.cyclo ./Core/Src/rpmsg_handler.d ./Core/Src/rpmsg_handler.o ./Core/Src/rpmsg_handler.su ./Core/Src/stm32mp1xx_hal_msp.cyclo ./Core/Src/stm32mp1xx_hal_msp.d ./Core/Src/stm32mp1xx_hal_msp.o ./Core/Src/stm32mp1xx_hal_msp.su ./Core/Src/stm32mp1xx_it.cyclo ./Core/Src/stm32mp1xx_it.d ./Core/Src/stm32mp1xx_it.o ./Core/Src/stm32mp1xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/task_buzzer.cyclo ./Core/Src/task_buzzer.d ./Core/Src/task_buzzer.o ./Core/Src/task_buzzer.su ./Core/Src/task_led.cyclo ./Core/Src/task_led.d ./Core/Src/task_led.o ./Core/Src/task_led.su ./Core/Src/tim.cyclo ./Core/Src/tim.d ./Core/Src/tim.o ./Core/Src/tim.su ./Core/Src/usart.cyclo ./Core/Src/usart.d ./Core/Src/usart.o ./Core/Src/usart.su ./Core/Src/ws2812b.cyclo ./Core/Src/ws2812b.d ./Core/Src/ws2812b.o ./Core/Src/ws2812b.su
=======
	-$(RM) ./Core/Src/dma.cyclo ./Core/Src/dma.d ./Core/Src/dma.o ./Core/Src/dma.su ./Core/Src/freertos.cyclo ./Core/Src/freertos.d ./Core/Src/freertos.o ./Core/Src/freertos.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/ipcc.cyclo ./Core/Src/ipcc.d ./Core/Src/ipcc.o ./Core/Src/ipcc.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32mp1xx_hal_msp.cyclo ./Core/Src/stm32mp1xx_hal_msp.d ./Core/Src/stm32mp1xx_hal_msp.o ./Core/Src/stm32mp1xx_hal_msp.su ./Core/Src/stm32mp1xx_it.cyclo ./Core/Src/stm32mp1xx_it.d ./Core/Src/stm32mp1xx_it.o ./Core/Src/stm32mp1xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/tim.cyclo ./Core/Src/tim.d ./Core/Src/tim.o ./Core/Src/tim.su ./Core/Src/usart.cyclo ./Core/Src/usart.d ./Core/Src/usart.o ./Core/Src/usart.su ./Core/Src/ws2812b.cyclo ./Core/Src/ws2812b.d ./Core/Src/ws2812b.o ./Core/Src/ws2812b.su
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9

.PHONY: clean-Core-2f-Src

