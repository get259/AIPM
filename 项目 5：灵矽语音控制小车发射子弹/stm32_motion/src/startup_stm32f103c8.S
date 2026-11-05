/* GCC startup for the existing STM32F10x medium-density application.
 * Vector order follows Start/startup_stm32f10x_md.s (STMicroelectronics, 2011).
 * SystemInit remains the original project implementation.
 */
.syntax unified
.cpu cortex-m3
.thumb

.section .isr_vector,"a",%progbits
.global __Vectors
.type __Vectors, %object
__Vectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .word WWDG_IRQHandler
    .word PVD_IRQHandler
    .word TAMPER_IRQHandler
    .word RTC_IRQHandler
    .word FLASH_IRQHandler
    .word RCC_IRQHandler
    .word EXTI0_IRQHandler
    .word EXTI1_IRQHandler
    .word EXTI2_IRQHandler
    .word EXTI3_IRQHandler
    .word EXTI4_IRQHandler
    .word DMA1_Channel1_IRQHandler
    .word DMA1_Channel2_IRQHandler
    .word DMA1_Channel3_IRQHandler
    .word DMA1_Channel4_IRQHandler
    .word DMA1_Channel5_IRQHandler
    .word DMA1_Channel6_IRQHandler
    .word DMA1_Channel7_IRQHandler
    .word ADC1_2_IRQHandler
    .word USB_HP_CAN1_TX_IRQHandler
    .word USB_LP_CAN1_RX0_IRQHandler
    .word CAN1_RX1_IRQHandler
    .word CAN1_SCE_IRQHandler
    .word EXTI9_5_IRQHandler
    .word TIM1_BRK_IRQHandler
    .word TIM1_UP_IRQHandler
    .word TIM1_TRG_COM_IRQHandler
    .word TIM1_CC_IRQHandler
    .word TIM2_IRQHandler
    .word TIM3_IRQHandler
    .word TIM4_IRQHandler
    .word I2C1_EV_IRQHandler
    .word I2C1_ER_IRQHandler
    .word I2C2_EV_IRQHandler
    .word I2C2_ER_IRQHandler
    .word SPI1_IRQHandler
    .word SPI2_IRQHandler
    .word USART1_IRQHandler
    .word USART2_IRQHandler
    .word USART3_IRQHandler
    .word EXTI15_10_IRQHandler
    .word RTCAlarm_IRQHandler
    .word USBWakeUp_IRQHandler
.size __Vectors, . - __Vectors

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler, %function
.thumb_func
Reset_Handler:
    /* Match Keil: configure the clock before C runtime initialization. */
    bl SystemInit
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
3:
    cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:
    bl __libc_init_array
    bl main
5:
    b 5b
.size Reset_Handler, . - Reset_Handler

/* No constructors are needed by the existing C application. */
.section .text._init,"ax",%progbits
.global _init
.type _init, %function
.thumb_func
_init:
    bx lr
.size _init, . - _init

.section .text.Default_Handler,"ax",%progbits
.type Default_Handler, %function
.thumb_func
Default_Handler:
    b .
.size Default_Handler, . - Default_Handler

.macro weak_handler name
    .weak \name
    .thumb_set \name, Default_Handler
.endm
weak_handler NMI_Handler
weak_handler HardFault_Handler
weak_handler MemManage_Handler
weak_handler BusFault_Handler
weak_handler UsageFault_Handler
weak_handler SVC_Handler
weak_handler DebugMon_Handler
weak_handler PendSV_Handler
weak_handler SysTick_Handler
weak_handler WWDG_IRQHandler
weak_handler PVD_IRQHandler
weak_handler TAMPER_IRQHandler
weak_handler RTC_IRQHandler
weak_handler FLASH_IRQHandler
weak_handler RCC_IRQHandler
weak_handler EXTI0_IRQHandler
weak_handler EXTI1_IRQHandler
weak_handler EXTI2_IRQHandler
weak_handler EXTI3_IRQHandler
weak_handler EXTI4_IRQHandler
weak_handler DMA1_Channel1_IRQHandler
weak_handler DMA1_Channel2_IRQHandler
weak_handler DMA1_Channel3_IRQHandler
weak_handler DMA1_Channel4_IRQHandler
weak_handler DMA1_Channel5_IRQHandler
weak_handler DMA1_Channel6_IRQHandler
weak_handler DMA1_Channel7_IRQHandler
weak_handler ADC1_2_IRQHandler
weak_handler USB_HP_CAN1_TX_IRQHandler
weak_handler USB_LP_CAN1_RX0_IRQHandler
weak_handler CAN1_RX1_IRQHandler
weak_handler CAN1_SCE_IRQHandler
weak_handler EXTI9_5_IRQHandler
weak_handler TIM1_BRK_IRQHandler
weak_handler TIM1_UP_IRQHandler
weak_handler TIM1_TRG_COM_IRQHandler
weak_handler TIM1_CC_IRQHandler
weak_handler TIM2_IRQHandler
weak_handler TIM3_IRQHandler
weak_handler TIM4_IRQHandler
weak_handler I2C1_EV_IRQHandler
weak_handler I2C1_ER_IRQHandler
weak_handler I2C2_EV_IRQHandler
weak_handler I2C2_ER_IRQHandler
weak_handler SPI1_IRQHandler
weak_handler SPI2_IRQHandler
weak_handler USART1_IRQHandler
weak_handler USART2_IRQHandler
weak_handler USART3_IRQHandler
weak_handler EXTI15_10_IRQHandler
weak_handler RTCAlarm_IRQHandler
weak_handler USBWakeUp_IRQHandler
.section .note.GNU-stack,"",%progbits
