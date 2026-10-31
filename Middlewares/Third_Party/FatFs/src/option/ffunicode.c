/*------------------------------------------------------------------------*/
/* Unicode Handling Functions (R0.12c interface, CP850 only)               */
/*------------------------------------------------------------------------*/
/* 为本工程 R0.12c 版 ff.c 提供长文件名(LFN)所需的 Unicode 转换函数：
 *   WCHAR ff_convert (WCHAR chr, UINT dir);   dir=0: Unicode->OEM, 其他: OEM->Unicode
 *   WCHAR ff_wtoupper (WCHAR chr);            字符大写化(文件名比较用)
 * CP850 转换表取自 FatFs 官方 R0.15 ffunicode.c，函数签名按 R0.12c ff.h 声明适配。
 * 注意：本文件仅覆盖 _CODE_PAGE==850 的单字节转换；如改用 936 等双字节
 * 页需替换为完整版 ffunicode.c。                                        */
/*------------------------------------------------------------------------*/
/*
/ Copyright (C) 2022, ChaN, all right reserved.
/
/ FatFs module is an open source software. Redistribution and use of FatFs in
/ source and binary forms, with or without modification, are permitted provided
/ that the following condition is met:
/
/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
*/

#include "ff.h"

#if _USE_LFN != 0	/* This module is blanked if in non-LFN configuration */

#if _CODE_PAGE == 850

static const WCHAR uc850[] = {	/*  CP850(Latin 1) to Unicode conversion table */
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x00C0, 0x00A9, 0x2563, 0x2551, 0x2557, 0x255D, 0x00A2, 0x00A5, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x00E3, 0x00C3, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
	0x00F0, 0x00D0, 0x00CA, 0x00CB, 0x00C8, 0x0131, 0x00CD, 0x00CE, 0x00CF, 0x2518, 0x250C, 0x2588, 0x2584, 0x00A6, 0x00CC, 0x2580,
	0x00D3, 0x00DF, 0x00D4, 0x00D2, 0x00F5, 0x00D5, 0x00B5, 0x00FE, 0x00DE, 0x00DA, 0x00DB, 0x00D9, 0x00FD, 0x00DD, 0x00AF, 0x00B4,
	0x00AD, 0x00B1, 0x2017, 0x00BE, 0x00B6, 0x00A7, 0x00F7, 0x00B8, 0x00B0, 0x00A8, 0x00B7, 0x00B9, 0x00B3, 0x00B2, 0x25A0, 0x00A0
};


WCHAR ff_convert (	/* Returns converted code, zero on error */
	WCHAR	chr,	/* Character code to be converted */
	UINT	dir		/* 0: Unicode->OEM, 1: OEM->Unicode */
)
{
	WCHAR c = 0;
	UINT i;

	if (chr < 0x80) {	/* ASCII is identical in both code sets */
		c = chr;
	} else {
		if (dir == 0) {	/* Unicode -> OEM code */
			for (i = 0; i < 128; i++) {
				if (uc850[i] == chr) {
					c = (WCHAR)(0x80 + i);
					break;
				}
			}
		} else {		/* OEM -> Unicode */
			if (chr < 0x100) {
				c = uc850[chr - 0x80];
			}
		}
	}

	return c;
}


WCHAR ff_wtoupper (	/* Returns up-converted code */
	WCHAR	chr		/* Unicode character code to be up-converted */
)
{
	if (chr >= 'a' && chr <= 'z') {
		return (WCHAR)(chr - 0x20);
	}
	switch (chr) {	/* CP850 extended lower -> upper */
	case 0x00E0: return 0x00C0;	/* a-grave */
	case 0x00E1: return 0x00C1;	/* a-acute */
	case 0x00E2: return 0x00C2;	/* a-circumflex */
	case 0x00E3: return 0x00C3;	/* a-tilde */
	case 0x00E4: return 0x00C4;	/* a-diaeresis */
	case 0x00E5: return 0x00C5;	/* a-ring */
	case 0x00E7: return 0x00C7;	/* c-cedilla */
	case 0x00E8: return 0x00C8;	/* e-grave */
	case 0x00E9: return 0x00C9;	/* e-acute */
	case 0x00EA: return 0x00CA;	/* e-circumflex */
	case 0x00EB: return 0x00CB;	/* e-diaeresis */
	case 0x00EC: return 0x00CC;	/* i-grave */
	case 0x00ED: return 0x00CD;	/* i-acute */
	case 0x00EE: return 0x00CE;	/* i-circumflex */
	case 0x00EF: return 0x00CF;	/* i-diaeresis */
	case 0x00F1: return 0x00D1;	/* n-tilde */
	case 0x00F2: return 0x00D2;	/* o-grave */
	case 0x00F3: return 0x00D3;	/* o-acute */
	case 0x00F4: return 0x00D4;	/* o-circumflex */
	case 0x00F5: return 0x00D5;	/* o-tilde */
	case 0x00F6: return 0x00D6;	/* o-diaeresis */
	case 0x00F8: return 0x00D8;	/* o-stroke */
	case 0x00F9: return 0x00D9;	/* u-grave */
	case 0x00FA: return 0x00DA;	/* u-acute */
	case 0x00FB: return 0x00DB;	/* u-circumflex */
	case 0x00FC: return 0x00DC;	/* u-diaeresis */
	case 0x00FD: return 0x00DD;	/* y-acute */
	case 0x00FE: return 0x00DE;	/* thorn */
	case 0x0131: return 0x0049;	/* dotless-i -> I */
	default:      return chr;
	}
}

#else
#error "ffunicode.c (R0.12c CP850) : unsupported _CODE_PAGE, use full version"
#endif

#endif /* _USE_LFN */
