/**
 ******************************************************************************
 * @file miniflac.c
 * @brief miniflac v1.1.3 单文件实现编译单元。
 *        miniflac 以 stb 风格单头文件分发（miniflac.h），在且仅在一个
 *        .c 里定义 MINIFLAC_IMPLEMENTATION 后包含即生成本体实现。
 *        上游: github.com/jprjr/miniflac（0BSD，仓库已删，经 teancom
 *        fork 获取 v1.1.3），本文件不修改上游代码。
 *
 * 内存模型（嵌入式关键结论）：
 *   - miniflac_t 内部无大缓冲（仅 ~0.6KB 状态机 + 32 系数 LPC 数组），
 *     位读取器直接扫调用方输入缓冲，不拷贝。
 *   - residual（残差）直接解码进调用方提供的逐声道输出缓冲
 *     （int32_t* samples[2]，每条 blocksize*4B），故 RAM 上限完全由
 *     调用方输出缓冲决定；flactest 命令按 blocksize<=4608 校验后解码。
 *   - 本编译单元未定义 MINIFLAC_ABORT_ON_ERROR，不引 stdlib/abort。
 ******************************************************************************
 */
#define MINIFLAC_IMPLEMENTATION
#include "miniflac.h"
