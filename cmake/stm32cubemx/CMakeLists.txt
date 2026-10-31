cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)
# STM32CubeMX generated symbols (macros)
set(MX_Defines_Syms 
	TFLM_RUNTIME 
	CMSIS_NN 
	TFLM_RUNTIME_USE_ALL_OPERATORS=0 
	TF_LITE_STATIC_MEMORY 
	TF_LITE_DISABLE_X86_NEON 
	TF_LITE_MCU_DEBUG_LOG 
	ARM_MATH 
	ARM_MATH_LOOPUNROLL 
	KERNELS_OPTIMIZED_FOR_SIZE 
	ARM_MATH_DSP 
	ARM_MATH_CM4 
	__FPU_PRESENT=1U 
	USE_HAL_DRIVER 
	STM32WB55xx
    $<$<CONFIG:Debug>:DEBUG>
)

# STM32CubeMX generated include paths
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32_WPAN/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_Device/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_Device/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/kissfft
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Include/Internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../X-CUBE-AI/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/flatbuffers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis/CMSIS/Core/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/gemmlowp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis/CMSIS/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/ruy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/lpm/tiny_lpm
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/tl
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/shci
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/utilities
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/template
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/svc/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/svc/Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32WBxx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/sequencer
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# STM32CubeMX generated application sources
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32_WPAN/Target/hw_ipcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32_WPAN/App/app_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32_WPAN/App/custom_stm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32_WPAN/App/custom_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/App/app_fatfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target/user_diskio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_Device/App/usb_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_Device/App/usbd_desc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_Device/App/usbd_storage_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_Device/Target/usbd_conf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/ipcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/rf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sai.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/app_entry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/app_debug.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/hw_timerserver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32_lpm_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/usart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32wbxx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32wbxx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../X-CUBE-AI/App/echodscnn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../startup_stm32wb55xx_cm4.s
)

# STM32 HAL/LL Drivers
set(STM32_Drivers_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/system_stm32wbxx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_pcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_pcd_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_ll_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_ll_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_hsem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_exti.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_ipcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_rtc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_sai.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_sai_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_spi_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32WBxx_HAL_Driver/Src/stm32wbxx_hal_uart_ex.c
)

# Drivers Midllewares


set(USB_Device_Library_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_bot.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_scsi.c
)
set(tensorflow_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/kissfft/tools/kiss_fftr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/kissfft/kiss_fft.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/TransposeFunctions/arm_transpose_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ReshapeFunctions/arm_reshape_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_batch_matmul_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_batch_matmul_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_fully_connected_get_buffer_sizes_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_fully_connected_get_buffer_sizes_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_fully_connected_per_channel_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_fully_connected_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_fully_connected_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_fully_connected_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_fully_connected_wrapper_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_vector_sum_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/FullyConnectedFunctions/arm_vector_sum_s8_s64.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SVDFunctions/arm_svdf_get_buffer_sizes_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SVDFunctions/arm_svdf_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SVDFunctions/arm_svdf_state_s16_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_elementwise_add_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_elementwise_add_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_elementwise_mul_acc_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_elementwise_mul_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_elementwise_mul_s16_batch_offset.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_elementwise_mul_s16_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_elementwise_mul_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_maximum_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/BasicMathFunctions/arm_minimum_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_1x1_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_1x1_s4_fast.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_1x1_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_1x1_s8_fast.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_1_x_n_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_1_x_n_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_even_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_get_buffer_sizes_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_get_buffer_sizes_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_get_buffer_sizes_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_wrapper_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_wrapper_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_convolve_wrapper_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_3x3_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_fast_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_get_buffer_sizes_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_get_buffer_sizes_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_get_buffer_sizes_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_s4_opt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_s8_opt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_wrapper_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_wrapper_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_depthwise_conv_wrapper_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_nn_depthwise_conv_s8_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_nn_mat_mult_kernel_row_offset_s8_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_nn_mat_mult_kernel_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_nn_mat_mult_kernel_s4_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_nn_mat_mult_kernel_s8_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_nn_mat_mult_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_transpose_conv_get_buffer_sizes_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_transpose_conv_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConvolutionFunctions/arm_transpose_conv_wrapper_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/LSTMFunctions/arm_lstm_unidirectional_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/LSTMFunctions/arm_lstm_unidirectional_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SoftmaxFunctions/arm_nn_softmax_common_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SoftmaxFunctions/arm_softmax_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SoftmaxFunctions/arm_softmax_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SoftmaxFunctions/arm_softmax_s8_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/SoftmaxFunctions/arm_softmax_u8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConcatenationFunctions/arm_concatenation_s8_w.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConcatenationFunctions/arm_concatenation_s8_x.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConcatenationFunctions/arm_concatenation_s8_y.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ConcatenationFunctions/arm_concatenation_s8_z.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/PoolingFunctions/arm_avgpool_get_buffer_sizes_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/PoolingFunctions/arm_avgpool_get_buffer_sizes_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/PoolingFunctions/arm_avgpool_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/PoolingFunctions/arm_avgpool_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/PoolingFunctions/arm_max_pool_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/PoolingFunctions/arm_max_pool_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ActivationFunctions/arm_nn_activation_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ActivationFunctions/arm_relu6_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ActivationFunctions/arm_relu_q15.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/ActivationFunctions/arm_relu_q7.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nntables.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_depthwise_conv_nt_t_padded_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_depthwise_conv_nt_t_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_depthwise_conv_nt_t_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_depthwise_conv_nt_t_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_lstm_calculate_gate_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_lstm_calculate_gate_s8_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_lstm_step_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_lstm_step_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mult_nt_interleaved_t_even_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mult_nt_t_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mult_nt_t_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mult_nt_t_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mult_nt_t_s8_s32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mul_core_1x_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mul_core_1x_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mul_core_4x_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_transpose_conv_row_s8_s32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mult_t_per_ch_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mult_t_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mult_t_s16_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mult_t_s4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mult_t_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mult_t_svdf_s8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mul_result_acc_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_vec_mat_mul_result_acc_s8_s16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_q7_to_q15_with_offset.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/NNSupportFunctions/arm_s8_to_s16_unordered_with_offset.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/tensorflow/third_party/cmsis_nn/Source/PadFunctions/arm_pad_s8.c
)
set(Utilities_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/lpm/tiny_lpm/stm32_lpm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Utilities/sequencer/stm32_seq.c
)
set(STM32_WPAN_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/tl/tl_mbox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/shci/shci.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/utilities/dbg_trace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/utilities/otp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/utilities/stm_list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/utilities/stm_queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/template/osal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto/ble_events.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto/ble_gen_aci.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto/ble_gap_aci.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto/ble_gatt_aci.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto/ble_hal_aci.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto/ble_hci_le.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/core/auto/ble_l2cap_aci.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/tl/hci_tl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/tl/hci_tl_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/tl/shci_tl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/interface/patterns/ble_thread/tl/shci_tl_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_WPAN/ble/svc/Src/svc_ctl.c
)
set(FatFs_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/diskio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/option/syscall.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/option/ffunicode.c
)

# Link directories setup
set(MX_LINK_DIRS

)
# Project static libraries
set(MX_LINK_LIBS 
    STM32_Drivers
    ${TOOLCHAIN_LINK_LIBRARIES}
    USB_Device_Library
	tensorflow
	Utilities
	STM32_WPAN
	FatFs
	
)
# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Create STM32_Drivers static library
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)
# Validate that STM32CubeMX code is compatible with C++ standard
if(CMAKE_CXX_STANDARD LESS 11)
    message(ERROR "Generated code requires C++11 or higher")
endif()

# Create USB_Device_Library static library
add_library(USB_Device_Library OBJECT)
target_sources(USB_Device_Library PRIVATE ${USB_Device_Library_Src})
target_link_libraries(USB_Device_Library PUBLIC stm32cubemx)

# Create tensorflow static library
add_library(tensorflow OBJECT)
target_sources(tensorflow PRIVATE ${tensorflow_Src})
target_link_libraries(tensorflow PUBLIC stm32cubemx)

# Create Utilities static library
add_library(Utilities OBJECT)
target_sources(Utilities PRIVATE ${Utilities_Src})
target_link_libraries(Utilities PUBLIC stm32cubemx)

# Create STM32_WPAN static library
add_library(STM32_WPAN OBJECT)
target_sources(STM32_WPAN PRIVATE ${STM32_WPAN_Src})
target_link_libraries(STM32_WPAN PUBLIC stm32cubemx)

# Create FatFs static library
add_library(FatFs OBJECT)
target_sources(FatFs PRIVATE ${FatFs_Src})
target_link_libraries(FatFs PUBLIC stm32cubemx)

# Add STM32CubeMX generated application sources to the project
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})

# Add libraries to the project
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
