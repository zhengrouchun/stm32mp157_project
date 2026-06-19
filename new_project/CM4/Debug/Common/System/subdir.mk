################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
<<<<<<< HEAD
D:/Stu15/IPC-Communication/RPMsg_UART_ADC/Common/System/system_stm32mp1xx.c 
=======
D:/Stu15/IPC-Communication/RPMsg_TEST/Common/System/system_stm32mp1xx.c 

O_SRCS += \
D:/Stu15/IPC-Communication/RPMsg_TEST/Common/System/system_stm32mp1xx.o 
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9

OBJS += \
./Common/System/system_stm32mp1xx.o 

C_DEPS += \
./Common/System/system_stm32mp1xx.d 


# Each subdirectory must supply rules for building sources it contributes
<<<<<<< HEAD
Common/System/system_stm32mp1xx.o: D:/Stu15/IPC-Communication/RPMsg_UART_ADC/Common/System/system_stm32mp1xx.c Common/System/subdir.mk
=======
Common/System/system_stm32mp1xx.o: D:/Stu15/IPC-Communication/RPMsg_TEST/Common/System/system_stm32mp1xx.c Common/System/subdir.mk
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DMETAL_MAX_DEVICE_REGIONS=2 -DSTM32MP157Dxx -DUSE_HAL_DRIVER -DCORE_CM4 -DDEBUG -DNO_ATOMIC_64_SUPPORT -DMETAL_INTERNAL -DVIRTIO_SLAVE_ONLY -c -I../Core/Inc -I../../Drivers/STM32MP1xx_HAL_Driver/Inc -I../../Drivers/STM32MP1xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32MP1xx/Include -I../../Drivers/CMSIS/Include -I../OPENAMP -I../../Middlewares/Third_Party/OpenAMP/open-amp/lib/include -I../../Middlewares/Third_Party/OpenAMP/libmetal/lib/include -I../../Middlewares/Third_Party/OpenAMP/virtual_driver -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../../Drivers/CMSIS/RTOS2/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Common-2f-System

clean-Common-2f-System:
	-$(RM) ./Common/System/system_stm32mp1xx.cyclo ./Common/System/system_stm32mp1xx.d ./Common/System/system_stm32mp1xx.o ./Common/System/system_stm32mp1xx.su

.PHONY: clean-Common-2f-System

