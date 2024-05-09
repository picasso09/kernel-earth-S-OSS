/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2016 MediaTek Inc.
 */


/*
 * This file is generated.
 * From 20181114_Latife_MDReg_remap.xlsx
 * With ap_md_reg_dump_code_gentool.py v0.1
 * Date 2018-11-14 13:02:08.887000
 */
enum MD_REG_ID {
	MD_REG_AP_MDSRC_REQ_NO_USE = 0,
	MD_REG_PC_MONITOR,
	MD_REG_PLL_REG,
	MD_REG_BUS,
	MD_REG_MDMCU_BUSMON,
	MD_REG_MDINFRA_BUSMON,
	MD_REG_ECT,
	MD_REG_TOPSM_REG,
	MD_REG_MD_RGU_REG,
	MD_REG_OST_STATUS,
	MD_REG_CSC_REG,
	MD_REG_ELM_REG,
	MD_REG_USIP,
};

void internal_md_dump_debug_register(unsigned int md_index);